// See LICENSE for details.

#include "gprocessor.hpp"

#include <sys/time.h>
#include <unistd.h>

#include <string>

#include "fetchengine.hpp"
#include "fmt/format.h"
#include "gmemory_system.hpp"
#include "report.hpp"
#include "tracer.hpp"

GProcessor::GProcessor(std::shared_ptr<Gmemory_system> gm, Hartid_t i)
    : Simu_base(gm, i)
    , FetchWidth(Config::get_integer("soc", "core", i, "fetch_width"))
    , IssueWidth(Config::get_integer("soc", "core", i, "issue_width"))
    , RetireWidth(Config::get_integer("soc", "core", i, "retire_width"))
    , RealisticWidth(RetireWidth < IssueWidth ? RetireWidth : IssueWidth)
    , InstQueueSize(Config::get_integer("soc", "core", i, "instq_size"))
    , MaxROBSize(Config::get_integer("soc", "core", i, "rob_size", 4))
    , memorySystem(gm)
    , rROB(Config::get_integer("soc", "core", i, "rob_size"))
    , ROB(MaxROBSize)
    , avgFetchWidth(fmt::format("P({})_avgFetchWidth", i))
    , rrobUsed(fmt::format("({})_rrobUsed", i))  // avg
    , robUsed(fmt::format("({})_robUsed", i))    // avg
    , nReplayInst(fmt::format("({})_nReplayInst", i))
    , nCommitted(fmt::format("({}):nCommitted", i))  // Should be the same as robUsed - replayed
    , noFetch(fmt::format("({}):noFetch", i))
    , noFetch2(fmt::format("({}):noFetch2", i))
    , pipeQ(i) {
  smt_size = Config::get_integer("soc", "core", i, "smt", 1, 32);

  lastReplay = 0;

  nStall[SmallWinStall]     = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nSmallWinStall", i));
  nStall[SmallROBStall]     = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nSmallROBStall", i));
  nStall[SmallREGStall]     = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nSmallREGStall", i));
  nStall[DivergeStall]      = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nDivergeStall", i));
  nStall[OutsLoadsStall]    = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nOutsLoadsStall", i));
  nStall[OutsStoresStall]   = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nOutsStoresStall", i));
  nStall[OutsBranchesStall] = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nOutsBranchesStall", i));
  nStall[ReplaysStall]      = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nReplaysStall", i));
  nStall[SyscallStall]      = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nSyscallStall", i));

  I(ROB.size() == 0);

  buildInstStats("ExeEngine");

#ifdef WAVESNAP_EN
  snap = std::make_unique<Wavesnap>();
#endif

  scb        = std::make_shared<Store_buffer>(i, gm);
  storeset   = std::make_shared<StoreSet>(i);
  prefetcher = std::make_shared<Prefetcher>(gm->getDL1(), i);

  use_stats = false;

  smt_fetch.fe.emplace_back(std::make_unique<FetchEngine>(i, gm));

  for (auto n = 1u; n < smt_size; ++n) {
    smt_fetch.fe.emplace_back(std::make_unique<FetchEngine>(i, gm, smt_fetch.fe[0]->ref_bpred()));
  }

  spaceInInstQueue = InstQueueSize;

  busy = false;
}

GProcessor::~GProcessor() {}

void GProcessor::buildInstStats(const std::string &txt) {
  for (const auto t : Opcodes) {
    nInst[t] = std::make_unique<Stats_cntr>(fmt::format("P({})_{}_{}:n", hid, txt, t));
  }
}

void SMT_fetch::update() {
  if (smt_lastTime != globalClock) {
    smt_lastTime = globalClock;
    smt_active   = smt_cnt;
    smt_cnt      = 1;
  } else {
    smt_turn++;
  }
  I(smt_active > 0);

  smt_turn--;
  if (smt_turn < 0) {
    if (smt_cnt == smt_active) {
      smt_turn = 0;
    } else {
      smt_turn = smt_active - 1;
    }
  }
}

std::shared_ptr<FetchEngine> SMT_fetch::fetch_next() {
  auto ptr = fe[smt_turn];

  update();

  return ptr;
}

void GProcessor::fetch() {
  // TODO: Move this to GProcessor (same as in OoOProcessor)
  I(eint);
  I(is_power_up());

  if (spaceInInstQueue < FetchWidth) {
    return;
  }

  auto ifid = smt_fetch.fetch_next();
  // must be before *bucket
  /*
  if (ifid->isBlocked()) {
    // fmt::print("fetch on {}\n", ifid->getMissDinst()->getID());
     return;
  }
*/

  auto     smt_hid = hid;  // FIXME: do SMT fetch
  IBucket *bucket  = pipeQ.pipeLine.newItem();

  if (ifid->isBlocked()) {
    Addr_t pc = ifid->getMissDinst()->getAddr() + 4;

    /*auto *dinst_br= ifid->getMissDinst();
    if(dinst_br->getCluster()->get_reg_pool() >= dinst_br->getCluster()->get_nregs()-7) {
      return;
    }*/
    if (bucket) {
      add_inst_transient_on_branch_miss(bucket, pc);
    }

    return;
  }
  // enable only when !ifid->isblocked() to flush transient before every new fetch
  // after a branch miss-prediction resolved at execute stage
  if (ifid->is_fetch_next_ready) {
    flush_transient_inst_on_fetch_ready();
  }

  if (bucket) {
    ifid->fetch(bucket, eint, smt_hid);
    if (!bucket->empty()) {
      avgFetchWidth.sample(bucket->size(), bucket->top()->has_stats());
      busy = true;
    }
  }
}
void GProcessor::flush_transient_inst_on_fetch_ready() {
  pipeQ.pipeLine.flush_transient_inst_from_buffer();
  flush_transient_inst_from_inst_queue();
  flush_transient_from_rob();
}

void GProcessor::flush_transient_from_rob() {
  // try the for loop scan

  while (!ROB.empty()) {
    auto *dinst = ROB.end_data();
    // makes sure isExecuted in preretire()

    if (!dinst->isTransient()) {
      ROB.push_pipe_in_cluster(dinst);
      ROB.pop_from_back();
      continue;
    }
    if (dinst->getCluster()->get_reg_pool() >= dinst->getCluster()->get_nregs() - 2) {
      break;
    }

    if (dinst->getCluster()->get_window_size() == dinst->getCluster()->get_window_maxsize()) {
      break;
    }
    /*if (dinst->hasDeps() || dinst->hasPending()) {
      ROB.push_pipe_in_cluster(dinst);
      ROB.pop_from_back();
      continue;
    }*/

    if (!dinst->isRetired() && dinst->isExecuted()) {
      // dinst->clearRATEntry();
      while (dinst->hasPending()) {
        Dinst *dstReady = dinst->getNextPending();
        I(dstReady->isTransient());
      }
      bool hasDest = (dinst->getInst()->hasDstRegister());
      if (hasDest) {
        dinst->getCluster()->add_reg_pool();
      }
      dinst->destroyTransientInst();
    } else if (dinst->isExecuting() || dinst->isIssued()) {
      dinst->mark_flush_transient();
      while (dinst->hasPending()) {
        Dinst *dstReady = dinst->getNextPending();
        I(dstReady->isTransient());
      }

      ROB.push_pipe_in_cluster(dinst);
    } else if (dinst->isRenamed()) {
      // Rename :RN
      if (dinst->getCluster()->get_reg_pool() >= dinst->getCluster()->get_nregs() - 3) {
        while (dinst->hasPending()) {
          Dinst *dstReady = dinst->getNextPending();
          I(dstReady->isTransient());
        }
        break;
      }
      dinst->markExecutedTransient();
      dinst->clearRATEntry();
      while (dinst->hasPending()) {
        Dinst *dstReady = dinst->getNextPending();
        I(dstReady->isTransient());
      }

      bool hasDest = (dinst->getInst()->hasDstRegister());
      if (hasDest) {
        dinst->getCluster()->add_reg_pool();
      }

      dinst->getCluster()->delEntry();
      dinst->destroyTransientInst();
    }

    ROB.pop_from_back();
  }
  while (!ROB.empty_pipe_in_cluster()) {
    auto *dinst = ROB.back_pipe_in_cluster();  // get last element from vector:back()

    /*if(dinst->getCluster()->get_reg_pool() >= dinst->getCluster()->get_nregs()-7) {
          ROB.push(dinst);//push in the end of ROB
          ROB.pop_pipe_in_cluster();//pop last element from buffer_ROB
          continue;
        }*/

    /*if(dinst->getCluster()->get_reg_pool() >= dinst->getCluster()->get_nregs()-2) {
          ROB.push(dinst);//push in the end of ROB
          ROB.pop_pipe_in_cluster();//pop last element from buffer_ROB
          continue;
    }*/

    if (dinst->is_flush_transient() && dinst->isExecuted() && !dinst->hasDeps() && !dinst->hasPending()) {
      if (dinst->getCluster()->get_window_size() < dinst->getCluster()->get_window_maxsize() - 1) {
        bool hasDest = (dinst->getInst()->hasDstRegister());
        if (hasDest) {
          dinst->getCluster()->add_reg_pool();
        }
        dinst->markExecutedTransient();
        dinst->clearRATEntry();
        dinst->getCluster()->delEntry();
        dinst->destroyTransientInst();
      }
    } else {
      ROB.push(dinst);  // push in the end of ROB
    }
    ROB.pop_pipe_in_cluster();  // pop last element from buffer_ROB
  }
}

/*void GProcessor::flush_transient_from_rob() {
//try the for loop scan
  while(!ROB.empty()) {
    //auto *dinst = ROB.top();
    auto *dinst = ROB.end_data();
    //if (!dinst->isTransient())
      //break;
    //makes sure isExecuted in preretire()
    bool  done  = dinst->getClusterResource()->preretire(dinst, false);
    if (!done) {
      //break;//FIXME
      //ROB.pop();
      ROB.push_pipe_in_cluster(dinst);
      ROB.pop_from_back();
      continue;
    }

    bool done_cluster = dinst->getCluster()->retire(dinst, false);

    if (!done_cluster) {
      //break;
      //ROB.pop();
      ROB.push_pipe_in_cluster(dinst);
      ROB.pop_from_back();
      continue;
    }
    if (dinst->isTransient()) {
         dinst->destroyTransientInst();
    }

  ROB.pop_from_back();
  }

  while(!ROB.empty_pipe_in_cluster()) {
    auto *dinst = ROB.back_pipe_in_cluster();//get last element from vector:back()
    ROB.pop_pipe_in_cluster();//pop last element
    ROB.push(dinst);//push in the end
  }


}
*/

void GProcessor::flush_transient_inst_from_inst_queue() {
  while (!pipeQ.instQueue.empty()) {
    auto *bucket = pipeQ.instQueue.top();
    if (bucket) {
      while (!bucket->empty()) {
        auto *dinst = bucket->top();
        bucket->pop();
        // I(dinst->isTransient());
        if (dinst->isTransient() && !dinst->is_present_in_rob()) {
          dinst->destroyTransientInst();
        }
      }
      if (bucket->empty()) {  // FIXME
        I(bucket->empty());
        pipeQ.pipeLine.doneItem(bucket);
      }
    }
    pipeQ.instQueue.pop();
  }
}

void GProcessor::add_inst_transient_on_branch_miss(IBucket *bucket, Addr_t pc) {
  int i = 0;
  // string a="LREG_R31";
  while (i < FetchWidth / 2) {
    // while (i< 3) {
    // auto  *alu_dinst = Dinst::create(Instruction(Opcode::iAALU, RegType::LREG_R3, RegType::LREG_R3, RegType::LREG_R3,
    // RegType::LREG_R3)
    auto *alu_dinst
        = Dinst::create(Instruction(Opcode::iAALU, RegType::LREG_R31, RegType::LREG_R30, RegType::LREG_R29, RegType::LREG_R31),
                        pc,
                        0,
                        0,
                        true);
    /*auto  *alu_dinst = Dinst::create(Instruction(Opcode::iBALU_RBRANCH, RegType::LREG_R17, RegType::LREG_R14, RegType::LREG_R16,
       RegType::LREG_R19) ,pc ,0 ,0 ,true);*/

    /*Addr_t addr    = 0x200;
    auto *alu_dinst= Dinst::create(Instruction(Opcode::iSALU_ST, RegType::LREG_R5, RegType::LREG_R6, RegType::LREG_R7,
    RegType::LREG_R8) ,pc ,addr ,0 ,true);*/

    if (alu_dinst) {
      std::cout << std::endl
                << "gProcessor::Yahoo!!Transient  Inst Created Opcode is " << alu_dinst->getInst()->getOpcodeName() << std::endl;
    }
    alu_dinst->setTransient();
    if (bucket) {
      // alu_dinst->setFetchTime();
      bucket->push(alu_dinst);
      Tracer::stage(alu_dinst, "TIF");
      // spaceInInstQueue -= bucket->size();
      // pipeQ.pipeLine.readyItem(bucket);//must bucket-> markedfetch()
    }
    i++;
    pc = pc + 4;
  }
  pipeQ.pipeLine.readyItem(bucket);  // must bucket-> markedfetch() after loop
}

/*GProcessor::GProcessor(std::shared_ptr<Gmemory_system> gm, Hartid_t i)
    : Simu_base(gm, i)
    , FetchWidth(Config::get_integer("soc", "core", i, "fetch_width"))
    , IssueWidth(Config::get_integer("soc", "core", i, "issue_width"))
    , RetireWidth(Config::get_integer("soc", "core", i, "retire_width"))
    , RealisticWidth(RetireWidth < IssueWidth ? RetireWidth : IssueWidth)
    , InstQueueSize(Config::get_integer("soc", "core", i, "instq_size"))
    , MaxROBSize(Config::get_integer("soc", "core", i, "rob_size", 4))
    , memorySystem(gm)
    , rROB(Config::get_integer("soc", "core", i, "rob_size"))
    , ROB(MaxROBSize)
    , avgFetchWidth(fmt::format("P({})_avgFetchWidth", i))
    , rrobUsed(fmt::format("({})_rrobUsed", i))  // avg
    , robUsed(fmt::format("({})_robUsed", i))    // avg
    , nReplayInst(fmt::format("({})_nReplayInst", i))
    , nCommitted(fmt::format("({}):nCommitted", i))  // Should be the same as robUsed - replayed
    , noFetch(fmt::format("({}):noFetch", i))
    , noFetch2(fmt::format("({}):noFetch2", i))
    , pipeQ(i) {
  smt_size = Config::get_integer("soc", "core", i, "smt", 1, 32);

  lastReplay = 0;

  nStall[SmallWinStall]     = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nSmallWinStall", i));
  nStall[SmallROBStall]     = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nSmallROBStall", i));
  nStall[SmallREGStall]     = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nSmallREGStall", i));
  nStall[DivergeStall]      = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nDivergeStall", i));
  nStall[OutsLoadsStall]    = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nOutsLoadsStall", i));
  nStall[OutsStoresStall]   = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nOutsStoresStall", i));
  nStall[OutsBranchesStall] = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nOutsBranchesStall", i));
  nStall[ReplaysStall]      = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nReplaysStall", i));
  nStall[SyscallStall]      = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nSyscallStall", i));

  I(ROB.size() == 0);

  buildInstStats("ExeEngine");

#ifdef WAVESNAP_EN
  snap = std::make_unique<Wavesnap>();
#endif

  scb        = std::make_shared<Store_buffer>(i, gm);
  storeset   = std::make_shared<StoreSet>(i);
  prefetcher = std::make_shared<Prefetcher>(gm->getDL1(), i);

  use_stats = false;

  smt_fetch.fe.emplace_back(std::make_unique<FetchEngine>(i, gm));

  for (auto n = 1u; n < smt_size; ++n) {
    smt_fetch.fe.emplace_back(std::make_unique<FetchEngine>(i, gm, smt_fetch.fe[0]->ref_bpred()));
  }

  spaceInInstQueue = InstQueueSize;

  busy = false;
}

GProcessor::~GProcessor() {}

void GProcessor::buildInstStats(const std::string &txt) {
  for (const auto t : Opcodes) {
    nInst[t] = std::make_unique<Stats_cntr>(fmt::format("P({})_{}_{}:n", hid, txt, t));
  }
}
*/
int32_t GProcessor::issue() {
  int32_t i = 0;  // Instructions executed counter

  I(!pipeQ.instQueue.empty());

  do {
    IBucket *bucket = pipeQ.instQueue.top();
    do {
      I(!bucket->empty());
      if (i >= IssueWidth) {
        return i;
      }

      I(!bucket->empty());

      Dinst *dinst = bucket->top();

      dinst->setGProc(this);

      std::cout << "gProcessor:: issueYahoo!!!Inst issued Opcode" << dinst->getInst()->getOpcodeName() << std::endl;
      StallCause c = add_inst(dinst);
      if (c != NoStall) {
        if (i < RealisticWidth) {
          nStall[c]->add(RealisticWidth - i, dinst->has_stats());
        }
        return i;
      }
      i++;

      bucket->pop();

    } while (!bucket->empty());

    pipeQ.pipeLine.doneItem(bucket);
    pipeQ.instQueue.pop();
  } while (!pipeQ.instQueue.empty());

  return i;
}

bool GProcessor::decode_stage() {
  if (!ROB.empty()) {
    use_stats = ROB.top()->has_stats();
  }

  bool new_clock = adjust_clock(use_stats);
  if (!new_clock) {
    return true;
  }

  // ID Stage (insert to instQueue)
  if (spaceInInstQueue >= FetchWidth) {
    IBucket *bucket = pipeQ.pipeLine.nextItem();
    if (bucket) {
      I(!bucket->empty());
      std::cout << "gProcessor:: decode Yahoo!!!Inst Opcode " << bucket->top()->getInst()->getOpcodeName() << std::endl;
      spaceInInstQueue -= bucket->size();
      pipeQ.instQueue.push(bucket);

    } else {
      noFetch2.inc(use_stats);
    }
  } else {
    noFetch.inc(use_stats);
  }

  // IBucket *bucket = pipeQ.instQueue.top();
  // pipeQ.instQueue.push(bucket);
  return false;
}
