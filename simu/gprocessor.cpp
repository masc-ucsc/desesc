// BISMILLAH HIR RAHMAR NIR RAHIM
//  See LICENSE for details.
#include "gprocessor.hpp"

#include <sys/time.h>
#include <unistd.h>

#include <string>

#include "addresspredictor.hpp"
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
  I(eint);
  I(is_power_up());

  if (spaceInInstQueue < FetchWidth) {
    return;
  }

  auto ifid = smt_fetch.fetch_next();
  if (ifid->isBlocked() && !do_random_transients) {
    return;
  }

  IBucket* bucket = pipeQ.pipeLine.newItem();

  auto smt_hid = hid;  // FIXME: do SMT fetch
  if (bucket) {
    if (ifid->isBlocked()) {
      I(do_random_transients);
      Addr_t pc = ifid->getMissDinst()->getAddr() + 4;  // FIXME: it should be last random pc+4
      add_inst_transient_on_branch_miss(bucket, pc);
    } else {
      ifid->fetch(bucket, eint, smt_hid, this);
      if (!bucket->empty()) {
        // printf("gprocessor::fetch:: completed bucket size is %ld\n", bucket->size());
        avgFetchWidth.sample(bucket->size(), bucket->top()->has_stats());
        busy = true;
      }
    }
  }
}

void GProcessor::flush_transient_inst_on_fetch_ready() {
#if 0
  if (!do_random_transients)
    return;
#endif

  flush_transient_inst_from_inst_queue();
  pipeQ.pipeLine.flush_transient_inst_from_buffer();
  flush_transient_from_rob();
}
void GProcessor::dump_rob()
// {{{1 Dump rob statistics
{
  uint32_t size = ROB.size();
  // printf("dump_ROB: ROB_size:%d\n", size);

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
  // printf("gprocessor::flush_transient_rob on before new fetch!!!\n");
  while (!ROB.empty()) {
    auto* dinst = ROB.end_data();
    // makes sure isExecuted in preretire()
    // printf("gprocessor::flush_transient_rob ROB size is %ld!!!\n", ROB.size());

    if (!dinst->isTransient()) {
      // printf("GPROCCESOR::flush_Rob ::NON TRANSIENT  instID %ld\n", dinst->getID());
      ROB.push_pipe_in_cluster(dinst);
      ROB.pop_from_back();
      continue;
    }
    /*if(dinst->mark_destroy_transient()) {
      continue;
    }*/

    // printf("GPROCCESOR::flush_Rob ::Entering::TRANSIENT  instID %ld\n", dinst->getID());
    dinst->clearRATEntry();
    // dinst->mark_to_be_destroyed_transient();
    /*if (dinst->getCluster()->get_reg_pool() >= dinst->getCluster()->get_nregs() - 2) {
      break;
    }*lima_may*/
    /*if (dinst->getCluster()->get_reg_pool() >= dinst->getCluster()->get_nregs() - 7) {
      printf("GPROCCESOR::flush_Rob ::TRANSIENT get_reg_pool() >= dinst->getCluster()->get_nregs() - 7:: instID %ld\n",
    dinst->getID()); dump_rob(); break; }lima_june24*/

    /*limasep if (dinst->getCluster()->get_window_size() == dinst->getCluster()->get_window_maxsize()) {
       printf("GPROCCESOR::flush_Rob :::>get_window_size() == dinst->getCluster()->get_window_maxsize( instID %ld\n",
     dinst->getID()); dump_rob(); break;
     }*/
    /*if (dinst->hasDeps() || dinst->hasPending()) {
      ROB.push_pipe_in_cluster(dinst);
      ROB.pop_from_back();
      continue;
    }*/

    // printf("GPROCCESOR::flush_Rob ::Entering  instID %ld\n", dinst->getID());
    // if(dinst)
    dinst->mark_destroy_transient();
    if (!dinst->isRetired() && dinst->isExecuted()) {
      // dinst->clearRATEntry();
      while (dinst->hasPending()) {
        // printf("GPROCCESOR::flush_Rob :: isRetired() Pending for instID %ld at @Clockcycle %ld\n", dinst->getID(), globalClock);
        Dinst* dstReady = dinst->getNextPending();
        I(dstReady->isTransient());
      }
      bool hasDest = (dinst->getInst()->hasDstRegister());
      if (hasDest && !dinst->is_try_flush_transient()) {
        // printf("GPROCCESOR::flush_Rob :: isRetired()  regpool++ destroying for instID %ld at @Clockcycle %ld\n",
        // dinst->getID(),
        // globalClock);
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
      // printf(
      //     "GPROCCESOR::flush_Rob :: isExecuting || isIssued()  :: not destroyed::windowsize is %d: for instID %ld at @Clockcycle
      //     "
      //     "%ld\n",
      //     dinst->getCluster()->get_window_size(),
      //     dinst->getID(),
      //     globalClock);
      // lima_june24
      bool hasDest = (dinst->getInst()->hasDstRegister());
      if (hasDest) {
        // printf("GPROCCESOR::flush_Rob :: isExecuting || isIssued()  regpool++ destroying for instID %ld at @Clockcycle %ld\n",
        //        dinst->getID(),
        //        globalClock);
        dinst->getCluster()->add_reg_pool();
        dinst->mark_try_flush_transient();
        // dinst->getCluster()->delEntry();
      }
      // lima_june24*/

      // printf("GPROCCESOR::flush_Rob ::mark flush:: isExecuting || isIssued instID %ld\n", dinst->getID());
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
      // printf("GPROCCESOR::flush_Rob :: isRenamed :: not destroyed::After windowsize is %d: for instID %ld at @Clockcycle %ld\n",
      //        dinst->getCluster()->get_window_size(),
      //        dinst->getID(),
      //        globalClock);
      // lima_june
      bool hasDest = (dinst->getInst()->hasDstRegister());
      if (hasDest) {
        // printf("GPROCCESOR::flush_Rob :: isRenamed  regpool++ destroying for instID %ld at @Clockcycle %ld\n",
        //        dinst->getID(),
        //        globalClock);
        dinst->getCluster()->add_reg_pool();
        dinst->mark_try_flush_transient();
      }
      // lima_june*/
      ROB.push_pipe_in_cluster(dinst);
    }

    // printf("GPROCCESOR::flush_Rob :: Poping from ROB.pop_from_back()  instID %ld at @Clockcycle %ld\n",
    //        dinst->getID(),
    //        globalClock);
    ROB.pop_from_back();
  }

  /*} else if (dinst->isRenamed()) {
        printf("GPROCCESOR::flush_Rob :: isTransient and (dinst->isRenamed()  instID %ld\n", dinst->getID());
        //Rename :RN
        if( dinst->getCluster()->get_reg_pool() >= dinst->getCluster()->get_nregs()-3){
          printf("GPROCCESOR::flush_Rob :: isTransient and (dinst->isRenamed() reg_pool>nregs instID %ld\n", dinst->getID());
          while (dinst->hasPending()) {
            Dinst *dstReady = dinst->getNextPending();
            I(dstReady->isTransient());
        }
          break;
        }
        printf("GPROCCESOR::flush_Rob : isRenamed markexecuted transient  instID %ld\n", dinst->getID());
        dinst->markExecutedTransient();
        //dinst->clearRATEntry();
        printf("GPROCCESOR::flush_Rob : clear RAT entry instID %ld\n", dinst->getID());
=======
    ///////startshere///if (!dinst->isRetired() && dinst->isExecuted()) {
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
>>>>>>> upstream/main
//ends here////////////////////
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

        dinst->clearRATEntry();

       //printf("GPROCCESOR::flush_Rob : isRenamed current instID %ld and getParentScr1 ID is: %ld and and getParentScr2 ID is :
%ld\n",
         //   dinst->getID(),dinst->getParenmSrc1()->getID(),dinst->getParentSrc1()->getID() );

       printf("GPROCCESOR::flush_Rob : isRenamed current instID %ld\n", dinst->getID());




        dinst->markExecutedTransient();
        dinst->getCluster()->delEntry();
        if(!dinst->hasDeps()){
        dinst->destroyTransientInst();
        } else {
        dinst->mark_to_be_destroyed_transient();
        }
    }//if_renamed*/

  // ROB.pop_from_back();
  // }
  while (!ROB.empty_pipe_in_cluster()) {
    auto* dinst = ROB.back_pipe_in_cluster();  // get last element from vector:back()

    if (dinst->is_flush_transient() && dinst->isExecuted() && !dinst->hasDeps() && !dinst->hasPending()) {
      if (dinst->getCluster()->get_window_size() < dinst->getCluster()->get_window_maxsize() - 1) {
        bool hasDest = (dinst->getInst()->hasDstRegister());
        if (hasDest && !dinst->is_try_flush_transient()) {
          dinst->getCluster()->add_reg_pool();
        }
        // if (!dinst->is_try_flush_transient()) {
        //   // printf("GPROCCESOR::flush_Rob :: ROB_back_in:: not destroyed::windowsize is %d: for instID %ld at @Clockcycle
        //   %ld\n",
        //   //        dinst->getCluster()->get_window_size(),
        //   //        dinst->getID(),
        //   //        globalClock);
        //   // dinst->getCluster()->delEntry();
        //   // printf("GPROCCESOR::flush_Rob :: ROB_back_in:: not destroyed::windowsize++ is %d: for instID %ld at @Clockcycle
        //   %ld\n",
        //   //        dinst->getCluster()->get_window_size(),
        //   //        dinst->getID(),
        //   //        globalClock);
        // }

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
  // printf("gprocessor::flush_transient_rob Leaving before new fetch!!!\n");
}

void GProcessor::flush_transient_inst_from_inst_queue() {
  // printf("gprocessor::flush_transient_inst_queue Entering before new fetch!!!\n");
  while (!pipeQ.instQueue.empty()) {
    auto* bucket = pipeQ.instQueue.end_data();
    if (bucket) {
      while (!bucket->empty()) {
        auto* dinst = bucket->end_data();
        if (dinst->isTransient()) {
          dinst->destroyTransientInst();
          bucket->pop_from_back();
          ++spaceInInstQueue;
        } else {
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
  // printf("gprocessor::flush_transient_inst_queue Leaving  before new fetch!!!\n");
}

/*
Addr_t GProcessor::random_addr_gen(){
      Addr_t addr = 0x200;
      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_int_distribution<> dis(1, 100);
      int randomNumber = dis(gen);
      return addr+(uint64_t)randomNumber;
}

uint64_t GProcessor::random_reg_gen( bool reg){
*/
// Addr_t GProcessor::random_addr_gen() {
//   Addr_t                          addr = 0x200;
//   std::random_device              rd;
//   std::mt19937                    gen(rd());
//   std::uniform_int_distribution<> dis(1, 100);
//   int                             randomNumber = dis(gen);
//   return addr + (uint64_t)randomNumber;
// }

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

    } else {
      alu_dinst = Dinst::create(
          Instruction(Opcode::iAALU, RegType::LREG_R13, RegType::LREG_R13, RegType::LREG_R0, RegType::LREG_InvalidOutput),
          pc,
          0,
          0,
          true);
    }

    alu_dinst->setTransient();
    if (bucket) {
      // alu_dinst->setFetchTime();
      bucket->push(alu_dinst);
      Tracer::stage(alu_dinst, "TIF");
    }
    i++;
    pc = pc + 4;
  }
  pipeQ.pipeLine.readyItem(bucket);  // must bucket-> markedfetch() after loop
}

int32_t GProcessor::issue() {
  int32_t i = 0;  // Instructions executed counter

  I(!pipeQ.instQueue.empty());

  do {
    IBucket* bucket = pipeQ.instQueue.top();
    do {
      I(!bucket->empty());
      if (i >= IssueWidth) {
        return i;
      }

      I(!bucket->empty());

      Dinst* dinst = bucket->top();
      // if (dinst->isTransient()) {
      //   printf("gProc::Issue Transient  gets from bucketsize %ld \n", bucket->size());
      // } else {
      //   printf("gProc::Issue  bucketsize %ld \n", bucket->size());
      // }
      //
      // printf("pProcessor::Issue Inst is %ld \n", dinst->getID());

      dinst->setGProc(this);

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
    IBucket* bucket = pipeQ.pipeLine.nextItem();
    if (bucket) {
      I(!bucket->empty());
      spaceInInstQueue -= bucket->size();
      pipeQ.instQueue.push(bucket);

    } else {
      noFetch2.inc(use_stats);
    }
  } else {
    noFetch.inc(use_stats);
  }

  return false;
}
