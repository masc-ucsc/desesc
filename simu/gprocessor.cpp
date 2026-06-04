//  See LICENSE for details.
#include "gprocessor.hpp"

#include <sys/time.h>
#include <unistd.h>

#include <string>

#include "addresspredictor.hpp"
#include "fetchengine.hpp"
#include "fmt/format.h"
#include "gmemory_system.hpp"
#include "port.hpp"
#include "report.hpp"
#include "tracer.hpp"

GProcessor::GProcessor(std::shared_ptr<Gmemory_system> gm, Hartid_t i)
    : Simu_base(gm, i)
    , FetchWidth(Config::get_integer("soc", "core", i, "fetch_width"))
    , IssueWidth(Config::get_integer("soc", "core", i, "issue_width"))
    , RetireWidth(Config::get_integer("soc", "core", i, "retire_width"))
    , RealisticWidth(RetireWidth < IssueWidth ? RetireWidth : IssueWidth)
    , InstQueueSize(Config::get_integer("soc", "core", i, "rename_instq_size"))
    , MaxROBSize(Config::get_integer("soc", "core", i, "rob_size", 4))
    , do_random_transients(Config::get_bool("soc", "core", i, "do_random_transients"))
    , memorySystem(gm)
    , rROB(Config::get_integer("soc", "core", i, "rob_size"))
    , ROB(MaxROBSize)
    , avgFetchWidth(fmt::format("P({})_avgFetchWidth", i))
    , rrobUsed(fmt::format("P({})_rrobUsed", i))  // avg
    , robUsed(fmt::format("P({})_robUsed", i))    // avg
    , nReplayInst(fmt::format("P({})_nReplayInst", i))
    , nCommitted(fmt::format("P({}):nCommitted", i))  // Should be the same as robUsed - replayed
    , noFetch(fmt::format("P({}):noFetch", i))
    , noFetch2(fmt::format("P({}):noFetch2", i))
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

  flushing_last_transientid = 0;
  last_transientid =0;
  busy = false;
}

GProcessor::~GProcessor() {}

void GProcessor::buildInstStats(const std::string& txt) {
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
  //printf("gprocessor::fetch:: Entering at @clockcycle %lu\n", globalClock);
  I(eint);
  I(is_power_up());
  
  auto ifid = smt_fetch.fetch_next();
 //unblock fetch starts here!!!
 if (!ifid->isBlocked() && do_random_transients) {
   //printf("gprocessor::fetch:: After Fetch is done after Br inst+ flush old transients at @clockcycle %lu\n", globalClock);
   flush_transient_inst_on_fetch_ready();
 }

  if (spaceInInstQueue < FetchWidth) {
    //printf("gprocessor::fetch:: spaceInInstQueue < FetchWidth) ::RETURN FALSE at @clockcycle %lu\n", globalClock);
    return;
  }

  //auto ifid = smt_fetch.fetch_next();
  if (ifid->isBlocked() && !do_random_transients) {
    //printf("gprocessor::fetch::ifid->isBlocked() && !do_random_transients ::RETURN FALSE at @clockcycle %lu\n", globalClock);
    return;
  }
 //unblock fetch starts here!!!
 /*if (!ifid->isBlocked() && do_random_transients) {
   //printf("gprocessor::fetch:: After Fetch is done after Br inst+ flush old transients at @clockcycle %lu\n", globalClock);
   flush_transient_inst_on_fetch_ready();
 }*/

  IBucket* bucket = pipeQ.pipeLine.newItem();

  auto smt_hid = hid;  // FIXME: do SMT fetch
  if (bucket) {
    if (ifid->isBlocked()) {
      //printf("gprocessor::fetch:: Fetch is blocked and add_transient() is added + bucket size is %zu at @clockcycle %llu\n",
             // bucket->size(),
             // globalClock);
      //I(do_random_transients);
      //if (ifid->is_ifid_control()){
      Addr_t pc = ifid->getMissDinst()->getAddr() + 4;  // FIXME: it should be last random pc+4
      add_inst_transient_on_branch_miss(bucket, pc);
      //ifid->reset_ifid_control();
      //}
    } else {
      // //printf("gprocessor::fetch::!ISBlocked() Sending fetch to fetchEngine at @clockcycle %llu\n", globalClock);
      ifid->fetch(bucket, eint, smt_hid, this);
      /*if (do_random_transients) {
        ////printf(
            // "gprocessor::fetch:: After Fetch is done after Br inst+ flush old transients+ bucket size is %zu at @clockcycle %lu\n",
             //bucket->size(),
             //globalClock);
        flush_transient_inst_on_fetch_ready();
      } */
      if (!bucket->empty()) {
        avgFetchWidth.sample(bucket->size(), bucket->top()->has_stats());
        busy = true;
      }
    }
  } else {
     //printf("gprocessor::fetch:: No FETCH !!! No Bucket-->pipeQ.pipeLine.newItem() at @clockcycle %lu\n", globalClock);
  }
     //printf("gprocessor::fetch:: Leaving FETCH true at @clockcycle %lu\n", globalClock);
}

void GProcessor::flush_transient_inst_on_fetch_ready() {
  if (!do_random_transients) {
    return;
  }

  flushing_last_transientid =last_transientid;
  pipeQ.pipeLine.set_flushing_from_last_transientid(flushing_last_transientid);

  flush_transient_inst_from_inst_queue();
  pipeQ.pipeLine.flush_transient_inst_from_buffer();
  pipeQ.pipeLine.flush_transient_inst_from_received_bucket();
  flush_transient_from_rob();
  flush_transient_from_scb();
  // Do NOT flush_transient_ports(): cbQ still holds Resource::executingCB /
  // executedCB pointing at these dinsts. If retire destroys them first, the
  // pool recycles the slot and the stale callbacks corrupt a fresh dinst.
}

void GProcessor::flush_transient_from_scb() {
  //printf("gprocessor::flush_transient_scb on before new fetch!!!\n");
  scb->flush_transient();
}

void GProcessor::flush_transient_ports() {
  for (auto& p : owned_ports) {
    p->flush_transient();
  }
}

void GProcessor::dump_rob()
// {{{1 Dump rob statistics
{
  uint32_t size = ROB.size();

  for (uint32_t i = 0; i < size; i++) {
    uint32_t pos = ROB.getIDFromTop(i);

    Dinst* dinst = ROB.getData(pos);
    if (dinst->isTransient()) {
      dinst->clearRATEntry();
    }

    dinst->dump("");
  }
}

void GProcessor::flush_transient_from_rob() {
  // try the for loop scan
  //printf("gprocessor::flush_transient_rob on before new fetch!!!\n");
  while (!ROB.empty()) {
    auto* dinst = ROB.end_data();
    // makes sure isExecuted in preretire()

    if (!dinst->isTransient()) {
      ROB.push_pipe_in_cluster(dinst);
      ROB.pop_from_back();
      continue;
    }
    /*if(dinst->mark_destroy_transient()) {
      continue;
    }*/

    dinst->clearRATEntry();

    // if(dinst)
    dinst->mark_destroy_transient();
    if (!dinst->isRetired() && dinst->isExecuted()) {
      // dinst->clearRATEntry();
      while (dinst->hasPending()) {
        Dinst* dstReady = dinst->getNextPending();
        I(dstReady->isTransient());
      }
      bool hasDest = (dinst->getInst()->hasDstRegister());
      if (hasDest && !dinst->is_try_flush_transient()) {
        dinst->getCluster()->add_reg_pool();
      }
      dinst->clearRATEntry();
      dinst->getCluster()->try_flushed(dinst);
      try_flush(dinst);
      // dinst->getCluster()->delEntry();:: happens automatically in cluster::ExecutedCluster ::delEntry()
      dinst->destroyTransientInst();
    } else if (dinst->isExecuting() || dinst->isIssued()) {
      if (dinst->is_try_flush_transient()) {
        ROB.push_pipe_in_cluster(dinst);
        ROB.pop_from_back();
        continue;
      }
      dinst->mark_flush_transient();
      dinst->clearRATEntry();
      dinst->getCluster()->try_flushed(dinst);
      // limasep2024dinst->mark_del_entry();
      dinst->getCluster()->del_entry_flush(dinst);

      bool hasDest = (dinst->getInst()->hasDstRegister());
      if (hasDest) {
        dinst->getCluster()->add_reg_pool();
        dinst->mark_try_flush_transient();
        // dinst->getCluster()->delEntry();
      }
      // lima_june24*/

      ROB.push_pipe_in_cluster(dinst);
    } else if (dinst->isRenamed()) {
      if (dinst->is_try_flush_transient()) {
        ROB.push_pipe_in_cluster(dinst);
        ROB.pop_from_back();
        continue;
      }
      dinst->clearRATEntry();
      dinst->getCluster()->try_flushed(dinst);
      // lima2024sepdinst->mark_del_entry();
      dinst->getCluster()->del_entry_flush(dinst);

      bool hasDest = (dinst->getInst()->hasDstRegister());
      if (hasDest) {
        dinst->getCluster()->add_reg_pool();
        dinst->mark_try_flush_transient();
      }
      // lima_june*/
      ROB.push_pipe_in_cluster(dinst);
    }

    ROB.pop_from_back();
  }

  while (!ROB.empty_pipe_in_cluster()) {
    auto* dinst = ROB.back_pipe_in_cluster();  // get last element from vector:back()

    if (dinst->is_flush_transient() && dinst->isExecuted() && !dinst->hasDeps() && !dinst->hasPending()) {
      if (dinst->getCluster()->get_window_size() < dinst->getCluster()->get_window_maxsize() - 1) {
        bool hasDest = (dinst->getInst()->hasDstRegister());
        if (hasDest && !dinst->is_try_flush_transient()) {
          dinst->getCluster()->add_reg_pool();
        }

        dinst->markExecutedTransient();
        dinst->clearRATEntry();
        dinst->getCluster()->try_flushed(dinst);
        try_flush(dinst);
        // 2024_sep//
        if (!dinst->is_del_entry()) {
          // limasep2024dinst->mark_del_entry();
          dinst->getCluster()->del_entry_flush(dinst);
        }
        // dinst->getCluster()->delEntry();
        dinst->destroyTransientInst();
      }
    } else {
      ROB.push(dinst);  // push in the end of ROB
      if (!dinst->is_del_entry() && dinst->isTransient()) {
        // limaspe2024dinst->mark_del_entry();
        dinst->getCluster()->del_entry_flush(dinst);
      }
      // added lima sep 2024
    }
    ROB.pop_pipe_in_cluster();  // pop last element from buffer_ROB
  }
}

void GProcessor::flush_remaining_transient_inst_from_inst_queue() {
  //printf("gprocessor::flush_transient_remaining_inst_queue Entering before new Transient_add_inst!!!\n");
  while (!pipeQ.instQueue.empty()) {
    auto* bucket = pipeQ.instQueue.end_data();
    if (bucket) {
      while (!bucket->empty() && bucket->is_transient()) {
        auto* dinst = bucket->end_data();
        if (dinst->isTransient()) {
          //printf("gprocessor::flush_transient_inst_remain_queue destroying inst %lu at @clockcycle %lu\n",
                  //dinst->getID(),
                  //globalClock);
          dinst->destroyTransientInst();
          bucket->pop_from_back();
          ++spaceInInstQueue;
        } else {
          //printf("gprocessor::flush_transient_inst_remain_queue NO Transient inst!! %lu at @clockcycle %lu\n",
                 //dinst->getID(),
                 //globalClock);
          return;
        }
      }
      if (bucket->empty()) {  // FIXME
        I(bucket->empty());
        pipeQ.pipeLine.doneItem(bucket);
      }
    }
    pipeQ.instQueue.pop_from_back();
  }
}

void GProcessor::flush_transient_inst_from_inst_queue() {
  //printf("gprocessor::flush_transient_inst_queue Entering before new fetch!!!\n");
  while (!pipeQ.instQueue.empty()) {
    auto* bucket = pipeQ.instQueue.end_data();
    if (bucket) {
      while (!bucket->empty()) {
        auto* dinst = bucket->end_data();
        if (dinst->isTransient()) {
          //printf("gprocessor::flush_transient_inst_queue destroying inst %lu at @clockcycle %lu\n", dinst->getID(), globalClock);
          dinst->destroyTransientInst();
          bucket->pop_from_back();
          ++spaceInInstQueue;
        } else {
          if (bucket->empty()) {  // FIXME
            I(bucket->empty());
            pipeQ.pipeLine.doneItem(bucket);
          }
          return;
        }
      }
      if (bucket->empty()) {  // FIXME
        I(bucket->empty());
        pipeQ.pipeLine.doneItem(bucket);
      }
    }
    pipeQ.instQueue.pop_from_back();
  }
}

uint64_t GProcessor::random_reg_gen(bool reg) {
  // std::random_device rd;
  //  std::mt19937 gen(rd());
  static std::mt19937 gen(55);
  if (reg) {
    std::uniform_int_distribution<> dis_reg(1, 31);
    int                             randomNumber_reg = dis_reg(gen);
    return (uint64_t)randomNumber_reg;

  } else {
    std::uniform_int_distribution<> dis_fp(33, 63);
    int                             randomNumber_fp = dis_fp(gen);
    return (uint64_t)randomNumber_fp;
  }
}

void GProcessor::add_inst_transient_on_branch_miss(IBucket* bucket, Addr_t pc) {
  int i = 0;
  while (i < 2) {
    Dinst*  alu_dinst;
    RegType src1 = RegType::LREG_INVALID;
    RegType src2 = RegType::LREG_INVALID;
    RegType dst1 = RegType::LREG_INVALID;
    RegType dst2 = RegType::LREG_InvalidOutput;
    bool    reg  = true;

    src1 = (RegType)random_reg_gen(reg);
    src2 = (RegType)random_reg_gen(reg);
    dst1 = (RegType)random_reg_gen(reg);

    if (rand() & 1) {
      alu_dinst = Dinst::create(Instruction(Opcode::iAALU, src1, src2, dst1, dst2), pc, 0, 0, true);
    } else if (rand() & 1) {
      reg  = false;
      src1 = (RegType)random_reg_gen(reg);
      src2 = (RegType)random_reg_gen(reg);
      dst1 = (RegType)random_reg_gen(reg);

      alu_dinst = Dinst::create(Instruction(Opcode::iCALU_FPALU, src1, src2, dst1, dst2), pc, 0, 0, true);
    } else if (rand() & 1) {
      alu_dinst = Dinst::create(Instruction(Opcode::iBALU_LBRANCH, src1, src2, dst1, dst2), pc, 0, 0, true);
      //printf("gprocessor::add_transient_inst creating BRANCH_TRANSIENT  %lu at @clockcycle %lu\n",
             //alu_dinst->getID(),
             //globalClock);

    } else {
      alu_dinst = Dinst::create(
          Instruction(Opcode::iAALU, RegType::LREG_R13, RegType::LREG_R13, RegType::LREG_R0, RegType::LREG_InvalidOutput),
          pc,
          0,
          0,
          true);
    }

    alu_dinst->setTransient();
    alu_dinst->set_spec();
    last_transientid = alu_dinst->getID();
    if (bucket) {
      //printf("gprocessor::add_transient_inst pushing in pipeline  %lu at @clockcycle %lu\n", alu_dinst->getID(), globalClock);
      alu_dinst->setFetchTime();
      bucket->push(alu_dinst);
      // flush_remaining_transient_inst_from_inst_queue();
      Tracer::stage(alu_dinst, "TIF");
    }
    i++;
    pc = pc + 4;
  }
  pipeQ.pipeLine.readyItem(bucket);  // must bucket-> markedfetch() after loop
}

int32_t GProcessor::issue() {
  // //printf("gprocessor::issue Entering Issue \n");
  int32_t i = 0;  // Instructions executed counter

  I(!pipeQ.instQueue.empty());
  // flush_remaining_transient_inst_from_inst_queue();

  do {
    IBucket* bucket = pipeQ.instQueue.top();
    do {
      I(!bucket->empty());
      if (i >= IssueWidth) {
        //printf("gprocessor::issue i<Issuewidth!!! return!!! \n");
        return i;
      }

      I(!bucket->empty());
      Dinst* dinst = bucket->top();

      dinst->setGProc(this);

      StallCause c = add_inst(dinst);
      //printf("gprocessor::issue inst  %llu at @clockcycle %llu\n", dinst->getID(), globalClock);
      if (c != NoStall) {
        if (i < RealisticWidth) {
          nStall[c]->add(RealisticWidth - i, dinst->has_stats());
        }
        return i;
      }
      i++;

      bucket->pop();

    } while (!bucket->empty());

    pipeQ.pipeLine.doneItem(bucket);  // make sure pipelineID<minItemCntr
    pipeQ.instQueue.pop();
  } while (!pipeQ.instQueue.empty());

  //printf("gprocessor::issue Exit inst\n");
  return i;
}

bool GProcessor::decode_stage() {
  if (!ROB.empty()) {
    use_stats = ROB.top()->has_stats();
  }

  bool new_clock = adjust_clock(use_stats);
  if (!new_clock) {
    //printf("gprocessor::decode !newclock @clockcycle %llu\n", globalClock);
    return true;
  }

  // pipeQ.pipeLine.flush_transient_inst_from_received_bucket();
  //  ID Stage (insert to instQueue)
  if (spaceInInstQueue >= FetchWidth) {
    //printf("gprocessor::decode  pipeline_nextitem at @clockcycle %llu\n", globalClock);
    IBucket* bucket = pipeQ.pipeLine.nextItem();

    // IBucket* temp = bucket;

    if (bucket) {
      I(!bucket->empty());
      spaceInInstQueue -= bucket->size();

      /*if (!bucket->top()->isTransient()) {
      spaceInInstQueue -= bucket->size();
      }*/
      pipeQ.instQueue.push(bucket);
      //printf("gprocessor::decode  pushing from pipelineQ --> InstQ at @clockcycle %llu\n", globalClock);

    } else {
      noFetch2.inc(use_stats);
    }
  } else {
    //printf("gprocessor::decode !spaceInInstQueue >= FetchWidth) at @clockcycle %llu\n", globalClock);
    noFetch.inc(use_stats);
  }

  //printf("gprocessor::decode Return False!!! at @clockcycle %llu\n", globalClock);
  return false;
}
