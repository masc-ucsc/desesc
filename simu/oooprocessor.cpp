// See LICENSE for details.

#include "oooprocessor.hpp"

#include <math.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <numeric>

#include "config.hpp"
#include "fastqueue.hpp"
#include "fetchengine.hpp"
#include "fmt/format.h"
#include "gmemory_system.hpp"
#include "memrequest.hpp"
#include "taskhandler.hpp"
#include "tracer.hpp"

//  #define ESESC_TRACE
//  #define ESESC_CODEPROFILE
//  #define ESESC_BRANCHPROFILE

// FIXME: to avoid deadlock, prealloc n to the n oldest instructions
// #define LATE_ALLOC_REGISTER

OoOProcessor::OoOProcessor(std::shared_ptr<Gmemory_system> gm, CPU_t i)
    /* constructor {{{1 */
    : GProcessor(gm, i)
    , MemoryReplay(Config::get_bool("soc", "core", i, "memory_replay"))
    , RetireDelay(Config::get_integer("soc", "core", i, "commit_delay"))
    , lsq(i, Config::get_integer("soc", "core", i, "ldq_size", 1))
    , retire_lock_checkCB(this)
    , clusterManager(gm, i, this)
#ifdef TRACK_TIMELEAK
    , avgPNRHitLoadSpec(fmt::format("P({})_avgPNRHitLoadSpec", i))
    , avgPNRMissLoadSpec(fmt::format("P({})_avgPNRMissLoadSpec", i))
#endif
#ifdef TRACK_FORWARDING
    , avgNumSrc(fmt::format("P({})_avgNumSrc", i))
    , avgNumDep(fmt::format("P({})_avgNumDep", i))
    , fwd0done0(fmt::format("P({})_fwd0done0", i))
    , fwd1done0(fmt::format("P({})_fwd1done0", i))
    , fwd1done1(fmt::format("P({})_fwd1done1", i))
    , fwd2done0(fmt::format("P({})_fwd2done0", i))
    , fwd2done1(fmt::format("P({})_fwd2done1", i))
    , fwd2done2(fmt::format("P({})_fwd2done2", i))
#endif
    , codeProfile(fmt::format("P({})_prof", i)) {

  spaceInInstQueue = InstQueueSize;

  codeProfile_trigger = 0;

  nTotalRegs = Config::get_integer("soc", "core", gm->getCoreId(), "num_regs", 32);

  flushing         = false;
  replayRecovering = false;
  replayID         = 0;

  last_state.dinst_ID = 0xdeadbeef;

  serialize = Config::get_integer("soc", "core", i, "replay_serialize_for");

  serialize_level       = 2;  // 0 full, 1 all ld, 2 same reg
  serialize_for         = 0;
  last_serialized       = 0;
  last_serializedST     = 0;
  forwardProg_threshold = 200;

  scooreMemory = Config::get_bool("soc", "core", gm->getCoreId(), "scoore_serialize");
}
/* }}} */

OoOProcessor::~OoOProcessor()
/* destructor {{{1 */
{
  // Nothing to do
}
/* }}} */

bool OoOProcessor::advance_clock_drain() {

  printf("OOOProc::advance_clock_drain ::decode_stage() is called\n");
  printf("OOOProc::advance_clock_drain ::decode_stage()::dump_rat is called\n");
  
  dump_rat();
  bool abort = decode_stage();
  
  if (abort || !busy) {
    return busy;
  }

  // RENAME Stage
  if (replayRecovering) {
    if ((rROB.empty() && ROB.empty())) {
      // Recovering done
      I(flushing);
      replayRecovering = false;
      flushing         = false;

      if ((lastReplay + 2 * forwardProg_threshold) < replayID) {
        serialize_level = 3;  // One over max to start with 2
      }
      if ((lastReplay + forwardProg_threshold) > replayID) {
        if (serialize_level) {
          serialize_level--;
        }
        serialize_for = serialize;
        // forwardProg_threshold = replayID - lastReplay;
        // serialize_for = forwardProg_threshold;
      }

      lastReplay = replayID;
    } else {
      nStall[ReplaysStall]->add(RealisticWidth, use_stats);
      retire();
      return true;
    }
  }

  if (!pipeQ.instQueue.empty()) {
    auto n = issue();
    spaceInInstQueue += n;
  } else if (ROB.empty() && rROB.empty() && !pipeQ.pipeLine.hasOutstandingItems()) {
    return false;
  }

  retire();

  return true;
}

bool OoOProcessor::advance_clock() {
  if (!TaskHandler::is_active(hid)) {
    return false;
  }

  Tracer::advance_clock();

//<<<<<<< HEAD
  printf("\nOOOProc::advance_clock() Leaving with pipeQ.InstQ.bucket size %ld\n",pipeQ.instQueue.size());
  printf("OOOProc::advance_clock ::fetch()::dump_rat is called\n");
  fetch();
  dump_rat();
  printf("OOOProc::advance_clock ::fetch() is called\n");
/*=======
  fetch();
>>>>>>> upstream/main*/

  return advance_clock_drain();
}

void OoOProcessor::executing(Dinst *dinst)
// {{{1 Called when the instruction starts to execute
{  
  if(dinst->isTransient()){
    printf("OOOProc::executing  Transient starts to dinstID %ld\n", dinst->getID());
    dinst->markExecutingTransient();
  } else {
    dinst->markExecuting();
  }
  
  printf("OOOProc::Executing::dump_rat is called\n");
  dump_rat();

  Tracer::stage(dinst, "EX");

#ifdef LATE_ALLOC_REGISTER
  if (dinst->getInst()->hasDstRegister()) {
    nTotalRegs--;
  }
#endif
#ifdef TRACK_FORWARDING
  if (dinst->has_stats()) {
    const Instruction *inst = dinst->getInst();
    avgNumSrc.sample(inst->getnsrc(), true);

    int nForward = 0;
    int nNeeded  = 0;
    if (inst->hasSrc1Register()) {
      nNeeded++;
      Time_t t = fwdDone[inst->getSrc1()];
      if ((t + 2) >= globalClock) {
        nForward++;
      }
    }
    if (inst->hasSrc2Register()) {
      nNeeded++;
      Time_t t = fwdDone[inst->getSrc2()];
      if ((t + 2) >= globalClock) {
        nForward++;
      }
    }

    if (nNeeded == 0) {
      fwd0done0.inc(true);
    } else if (nNeeded == 1) {
      if (nForward) {
        fwd1done1.inc(true);
      } else {
        fwd1done0.inc(true);
      }
    } else {
      if (nForward == 2) {
        fwd2done2.inc(true);
      } else if (nForward == 1) {
        fwd2done1.inc(true);
      } else {
        fwd2done0.inc(true);
      }
    }
  }
#endif
}
// 1}}}
//
void OoOProcessor::executed([[maybe_unused]] Dinst *dinst) {
  
  printf("OOOProc::Executed::dump_rat is called\n");
  dump_rat();
  if(dinst->isTransient())
    printf("OOOProc::executed Transientinst starts to executed\n");
  else 
    printf("OOOProc::executed  starts to dinstID %ld\n", dinst->getID());

#ifdef TRACK_FORWARDING
  fwdDone[dinst->getInst()->getDst1()] = globalClock;
  fwdDone[dinst->getInst()->getDst2()] = globalClock;
#endif
}
void OoOProcessor::flushed(Dinst *dinst)
// {{{1 Called when the instruction is flushed
{
  (void)dinst;
}

StallCause OoOProcessor::add_inst(Dinst *dinst) {
  if (replayRecovering && dinst->getID() > replayID) {
    Tracer::stage(dinst, "Wrep");
    return ReplaysStall;
  }

  if ((ROB.size() + rROB.size()) >= (MaxROBSize - 1)) {
    Tracer::stage(dinst, "Wrob");
    return SmallROBStall;
  }

  const Instruction *inst = dinst->getInst();

  if (nTotalRegs <= 0) {
    Tracer::stage(dinst, "Wreg");
    return SmallREGStall;
  }

  auto cluster = dinst->getCluster();
  if (!cluster) {
    auto res = clusterManager.getResource(dinst);
    cluster  = res->getCluster();
    dinst->set(cluster, res);
  }

  StallCause sc = cluster->canIssue(dinst);
  if (sc != NoStall) {
    Tracer::stage(dinst, "Wcls");
    return sc;
  }

  // if no stalls were detected do the following:
  //
  // BEGIN INSERTION (note that cluster already inserted in the window)
  // dinst->dump("");

#ifndef LATE_ALLOC_REGISTER
  if (inst->hasDstRegister()) {
    nTotalRegs--;
  }
#endif

  if (!scooreMemory) {  // no dynamic serialization for tradcore
    printf("ooop::add_inst !scooreMemory dinstID %ld\n", dinst->getID());
    if (serialize_for > 0 && !replayRecovering) {
      serialize_for--;
      if (inst->isMemory() && dinst->isSrc3Ready()) {
        if (last_serialized && !last_serialized->isExecuted()) {
          // last_serialized->addSrc3(dinst); FIXME
          // MSG("addDep3 %8ld->%8lld %lld",last_serialized->getID(), dinst->getID(), globalClock);
        }
        last_serialized = dinst;
      }
    }
  } else {
    if (serialize_for > 0 && !replayRecovering) {
      serialize_for--;

      if (serialize_level == 0) {
        // Serialize all the memory operations
        if (inst->isMemory() && dinst->isSrc3Ready()) {
          if (last_serialized && !last_serialized->isIssued()) {
            last_serialized->addSrc3(dinst);
          }
          last_serialized = dinst;
        }
      } else if (serialize_level == 1) {
        // Serialize stores, and loads depend on stores (no loads on loads)
        if (inst->isLoad() && dinst->isSrc3Ready()) {
          if (last_serializedST && !last_serializedST->isIssued()) {
            last_serializedST->addSrc3(dinst);
          }
          last_serialized = dinst;
        }
        if (inst->isStore() && dinst->isSrc3Ready()) {
          if (last_serialized && !last_serialized->isIssued()) {
            last_serialized->addSrc3(dinst);
          }
          last_serializedST = dinst;
        }
      } else {
        // Serialize if same register is being accessed
        if (is_arch(inst->getSrc1())) {
          last_serializeLogical = inst->getSrc1();
        } else if (last_serializePC != dinst->getPC()) {
          last_serializeLogical = RegType::LREG_InvalidOutput;
        }
        last_serializePC = dinst->getPC();

        if (is_arch(last_serializeLogical)) {
          if (inst->isMemory()) {
            if (serializeRAT[last_serializeLogical]) {
              if (inst->isLoad()) {
                if (serializeRAT[last_serializeLogical]->getInst()->isStore()) {
                  serializeRAT[last_serializeLogical]->addSrc3(dinst);
                }
              } else {
                serializeRAT[last_serializeLogical]->addSrc3(dinst);
              }
            }

            dinst->setSerializeEntry(&serializeRAT[last_serializeLogical]);
            serializeRAT[last_serializeLogical] = dinst;
          } else {
            printf("ooop::add_inst serializeRAT  dinstID %ld\n", dinst->getID());
            serializeRAT[inst->getDst1()] = nullptr;
            serializeRAT[inst->getDst2()] = nullptr;
          }
        }
      }
    }
  }

  nInst[inst->getOpcode()]->inc(dinst->has_stats());  // FIXME: move to cluster

  ROB.push(dinst);
  dinst->set_present_in_rob();
  I(dinst->getCluster() != 0);  // Resource::schedule must set the resource field

  printf("OOOProc::add_inst and dumprat before adding in RAT%ld\n",dinst->getID());
  dump_rat();
  int n = 0;
  if (!dinst->isSrc2Ready()) {
    // It already has a src2 dep. It means that it is solved at
    // retirement (Memory consistency. coherence issues)
    
    printf("OOOProc::add_inst !dinst->isSrc2Ready(): :: src2 RAW dep for%ld \n",dinst->getID());
    if (RAT[inst->getSrc1()]) {
      RAT[inst->getSrc1()]->addSrc1(dinst);
      printf("OOOProc::add_inst addSrc1 %ld\n",dinst->getID());
      n++;
      // MSG("addDep0 %8ld->%8lld %lld",RAT[inst->getSrc1()]->getID(), dinst->getID(), globalClock);
    }
  } else {
    printf("OOOProc::add_inst dinst->isSrc2Ready():: no src2 dep:: for %ld \n",dinst->getID());
    
    if (RAT[inst->getSrc1()]) {
      printf("OOOProc::add_inst addSrc1 %ld\n",dinst->getID());
      RAT[inst->getSrc1()]->addSrc1(dinst);
      n++;
      // MSG("addDep1 %8ld->%8lld %lld",RAT[inst->getSrc1()]->getID(), dinst->getID(), globalClock);
    } else {
       printf("OOOProc::add_inst dinst->isSrc2Ready():: no RAT Src1 entry for %ld \n",dinst->getID());
    }


    if (RAT[inst->getSrc2()]) {
      printf("OOOProc::add_inst addSrc2 %ld\n",dinst->getID());
      RAT[inst->getSrc2()]->addSrc2(dinst);
      n++;
      // MSG("addDep2 %8ld->%8lld %lld",RAT[inst->getSrc2()]->getID(), dinst->getID(), globalClock);
     } else {
        printf("OOOProc::add_inst dinst->isSrc2Ready():: no RAT Src2 entry for %ld \n",dinst->getID());
     }

  }
#ifdef TRACK_FORWARDING
  avgNumSrc.sample(inst->getnsrc(), dinst->has_stats());
  avgNumDep.sample(n, dinst->has_stats());
#else
  (void)n;
#endif

  printf("OOOPROCCESOR::add_inst : RAT entry instID %ld\n", dinst->getID());  
  dinst->setRAT1Entry(&RAT[inst->getDst1()]);
  dinst->setRAT2Entry(&RAT[inst->getDst2()]);

  I(!dinst->isExecuted());

  dinst->getCluster()->add_inst(dinst);
//printLimas
  if (!dinst->isExecuted()) {
    RAT[inst->getDst1()] = dinst;
    RAT[inst->getDst2()] = dinst;
  }

  I(dinst->getCluster());

  dinst->markRenamed();
  Tracer::stage(dinst, "RN");
//<<<<<<< HEAD
  printf("OOOPROCCESOR::add_inst :  done rename instID %ld\n", dinst->getID());  

  printf("OOOProc::add_inst and dumprat after adding in RAT%ld\n",dinst->getID());
  dump_rat();
//=======

//>>>>>>> upstream/main
#ifdef WAVESNAP_EN
  // add instruction to wavesnap
  if (!SINGLE_WINDOW) {
    if (WITH_SAMPLING) {
      if (dinst->has_stats()) {
        snap->add_instruction(dinst);
      }
    } else {
      snap->add_instruction(dinst);
    }
  }
#endif

//<<<<<<< HEAD
  //if(!dinst->is_in_cluster()) {
    //dinst->getCluster()->add_inst_retry(dinst);
  //lima}

  printf("OOOProc::add_inst %ld Exiting add_inst with NoStall \n", dinst->getID());
//=======
//>>>>>>> upstream/main
  return NoStall;
}
/* }}} */

void OoOProcessor::retire_lock_check()
/* Detect simulator locks and flush the pipeline {{{1 */
{
  RetireState state;
  if (!rROB.empty()) {
    state.r_dinst    = rROB.top();
    state.r_dinst_ID = rROB.top()->getID();
  }

  if (!ROB.empty()) {
    state.dinst    = ROB.top();
    state.dinst_ID = ROB.top()->getID();
  }

  if (last_state == state && TaskHandler::is_active(hid)) {
    fmt::print("Lock detected in P({}), flushing pipeline\n", hid);
    I(false);
  }

  last_state = state;

  retire_lock_checkCB.scheduleAbs(globalClock + 100000);
}
/* }}} */

void OoOProcessor::retire() {
//<<<<<<< HEAD
  printf("\nOOOProc::retire Entering  \n");
  printf("\nOOOProc::retire dump_rat starting  \n");
  dump_rat();
#ifdef ENABLE_LDBP
  int64_t gclock = int64_t(clockTicks.getDouble());
  if (gclock != power_clock) {
    power_clock = gclock;
    if (power_save_mode_ctr <= (MAX_POWER_SAVE_MODE_CTR + 1)) {
      power_save_mode_ctr++;
    }
    if (power_save_mode_ctr >= MAX_POWER_SAVE_MODE_CTR) {
      // reset tables and power off
      ldbp_power_save_cycles.inc(true);
      tmp_power_clock++;
      // MSG("global=%d tmp_power_clock=%d", gclock, tmp_power_clock);
      if (power_save_mode_ctr == MAX_POWER_SAVE_MODE_CTR) {
        power_save_mode_table_reset();
      }
    }
  }
#endif
//=======
//>>>>>>> upstream/main
  // Pass all the ready instructions to the rrob
  while (!ROB.empty()) {
    auto *dinst = ROB.top();

    I(dinst->getCluster());
    bool done = dinst->getClusterResource()->preretire(dinst, flushing);
    // Addr_t ppc = dinst->getPC();
    // MSG("MV");
    GI(flushing && dinst->isExecuted(), done);
    if (!done) {
      break;
    }

    I(dinst->getCluster());
    if (dinst->isTransient()) {
      bool done_cluster = dinst->getCluster()->retire(dinst, flushing);
      if (!done_cluster) {
        break;
      }
    }
    // Tracer::event(dinst, "PNR");
    if (dinst->is_flush_transient()) {
      if (!dinst->isExecuted()) {
        dinst->markExecutedTransient();
        dinst->clearRATEntry();
        // dinst->getCluster()->delEntry();
        while (dinst->hasPending()) {
          Dinst *dstReady = dinst->getNextPending();
          I(dstReady->isTransient());
        }
      } else {
        while (dinst->hasPending()) {
          Dinst *dstReadyPending = dinst->getNextPending();
          I(dstReadyPending->isTransient());
        }
      }

      if (dinst->getInst()->hasDstRegister()) {
        nTotalRegs++;
      }
      Tracer::event(dinst, "PNR");
      dinst->destroyTransientInst();
      ROB.pop();
      continue;

    }  // is_flush_transient_if

    if (dinst->isTransient()) {
      if (!dinst->isExecuted()) {
        dinst->markExecutedTransient();
        dinst->clearRATEntry();
        // dinst->getCluster()->delEntry();
        // Tracer::stage(dinst, "TR");

        while (dinst->hasPending()) {
          Dinst *dstReady = dinst->getNextPending();
          I(dstReady->isTransient());
        }
      }

      if (dinst->getInst()->hasDstRegister()) {
        nTotalRegs++;
      }
      while (dinst->hasPending()) {
        Dinst *dstReady = dinst->getNextPending();
        I(dstReady->isTransient());
      }

      Tracer::event(dinst, "PNR");
      dinst->destroyTransientInst();
      ROB.pop();
      // return;
      continue;
    } else {
      Tracer::event(dinst, "PNR");
      rROB.push(dinst);
      ROB.pop();
    }
  }  //! ROB.empty()_loop_end

  // if (!ROB.empty() && ROB.top()->has_stats() && !ROB.top()->isTransient()) {

  if (!ROB.empty() && ROB.top()->has_stats()) {
    robUsed.sample(ROB.size(), true);
#ifdef TRACK_TIMELEAK
    int total_hit  = 0;
    int total_miss = 0;
    for (uint32_t i = 0; i < ROB.size(); i++) {
      uint32_t pos   = ROB.getIDFromTop(i);
      Dinst   *dinst = ROB.getData(pos);

      if (!dinst->has_stats()) {
        continue;
      }
      if (!dinst->getInst()->isLoad()) {
        continue;
      }
      if (dinst->isPerformed()) {
        continue;
      }

      // if (!dinst->isTransient()) {
      if (dinst->isFullMiss()) {
        total_miss++;
      } else {
        total_hit++;
      }
      //}
    }
    avgPNRHitLoadSpec.sample(total_hit, true);
    avgPNRMissLoadSpec.sample(true, total_miss);
#endif
  }  // ROB_Load_Spec_sampling end

  if (!rROB.empty()) {
    rrobUsed.sample(rROB.size(), rROB.top()->has_stats());

#ifdef ESESC_CODEPROFILE
    if (rROB.top()->has_stats()) {
      if (codeProfile_trigger <= clockTicks.getDouble()) {
        Dinst *dinst = rROB.top();

        codeProfile_trigger = clockTicks.getDouble() + 121;

        double wt    = dinst->getIssuedTime() - dinst->getRenamedTime();
        double et    = dinst->getExecutedTime() - dinst->getIssuedTime();
        bool   flush = dinst->isBranchMiss();

        codeProfile.sample(rROB.top()->getPC(),
                           nCommitted.getDouble(),
                           clockTicks.getDouble(),
                           wt,
                           et,
                           flush,
                           dinst->isPrefetch(),
                           dinst->isBranchMiss_level1(),
                           dinst->isBranchMiss_level2(),
                           dinst->isBranchMiss_level3(),
                           dinst->isBranchHit_level1(),
                           dinst->isBranchHit_level2(),
                           dinst->isBranchHit_level3(),
                           dinst->isBranch_hit2_miss3(),
                           dinst->isBranch_hit3_miss2());
      }
    }
#endif
  }

  // rROB_loop_starts
  for (uint16_t i = 0; i < RetireWidth && !rROB.empty(); i++) {
    Dinst *dinst = rROB.top();
    dinst->mark_rrob();

    if (last_serialized == dinst) {
      last_serialized = 0;
    }
    if (last_serializedST == dinst) {
      last_serializedST = 0;
    }

    if ((dinst->getExecutedTime() + RetireDelay) >= globalClock) {
#ifdef SUPERDUMP
      if (rROB.size() > 8) {
        dinst->getInst()->dump("not ret");
        printf("----------------------\n");
        dumpROB();
      }
#endif
      break;
    }

    I(dinst->getCluster());

    bool done = dinst->getCluster()->retire(dinst, flushing);
    if (!done) {
      break;
    }

    Hartid_t smt_hid = dinst->getFlowId();
    if (dinst->isReplay()) {
      flushing     = true;
      flushing_fid = smt_hid;  // It can be different from hid due to SMT
    }

    nCommitted.inc(!flushing && dinst->has_stats());

#ifdef ESESC_BRANCHPROFILE
    if (dinst->getInst()->isBranch() && dinst->has_stats()) {
      codeProfile.sample(dinst->getPC(),
                         dinst->getID(),
                         0,
                         dinst->isBiasBranch() ? 1.0 : 0,
                         0,
                         dinst->isBranchMiss(),
                         dinst->isPrefetch(),
                         dinst->getLBType(),
                         dinst->isBranchMiss_level1(),
                         dinst->isBranchMiss_level2(),
                         dinst->isBranchMiss_level3(),
                         dinst->isBranchHit_level1(),
                         dinst->isBranchHit_level2(),
                         dinst->isBranchHit_level3(),
                         dinst->isBranch_hit2_miss3(),
                         dinst->isBranch_hit3_miss2(),
                         dinst->isTrig_ld1_pred(),
                         dinst->isTrig_ld1_unpred(),
                         dinst->isTrig_ld2_pred(),
                         dinst->isTrig_ld2_unpred(),
                         dinst->get_trig_ld_status());
    }
#endif

#ifdef ESESC_TRACE
    fmt::print("TR {:<8} {:<8x} r{:<2},r{:<2}= r{:<2} op={} r{:<2} ft:{} rt:{} it:{} et:{} @{}\n",
               dinst->getID(),
               dinst->getPC(),
               dinst->getInst()->getDst1(),
               dinst->getInst()->getDst2(),
               dinst->getInst()->getSrc1(),
               dinst->getInst()->getOpcode(),
               dinst->getInst()->getSrc2(),
               globalClock - dinst->getFetchedTime(),
               globalClock - dinst->getRenamedTime(),
               globalClock - dinst->getIssuedTime(),
               globalClock - dinst->getExecutedTime(),
               globalClock);
#endif

#ifdef WAVESNAP_EN
    // updading wavesnap instruction windows
    if (SINGLE_WINDOW) {
      if (WITH_SAMPLING) {
        if (!flushing && dinst->has_stats()) {
          snap->update_single_window(dinst, (uint64_t)globalClock);
        }
      } else {
        snap->update_single_window(dinst, (uint64_t)globalClock);
      }
    } else {
      if (WITH_SAMPLING) {
        if (dinst->has_stats()) {
          snap->update_window(dinst, (uint64_t)globalClock);
        }
      } else {
        snap->update_window(dinst, (uint64_t)globalClock);
      }
    }
#endif
    if (dinst->getInst()->hasDstRegister()) {
      nTotalRegs++;
    }

    if (!dinst->getInst()->isStore()) {  // Stores can perform after retirement
      I(dinst->isPerformed());
    }

    if (dinst->isPerformed()) {  // Stores can perform after retirement
      dinst->destroy();
    }

    rROB.pop();
//<<<<<<< HEAD
  }// !rROB.empty()_loop_ends
   
  printf("OOOProcessor::retire  Exiting from retire \n");
  printf("\nOOOProc::retire dump_rat Leaving \n");
  dump_rat();
//=======
  //}  // !rROB.empty()_loop_ends
//>>>>>>> upstream/main
}

void OoOProcessor::replay(Dinst *target)
/* trigger a processor replay {{{1 */
{
  if (serialize_for) {
    return;
  }

  I(serialize_for <= 0);
  // Same load can be marked by several stores in a OoO core : I(replayID != target->getID());
  I(target->getInst()->isLoad());

  if (!MemoryReplay) {
    return;
  }
  target->markReplay();
  Tracer::event(target, "replay");

  if (replayID < target->getID()) {
    replayID = target->getID();
  }

  if (replayRecovering) {
    return;
  }
  replayRecovering = true;

  // Count the # instructions wasted
  size_t fetch2rename = 0;
  fetch2rename += (InstQueueSize - spaceInInstQueue);
  fetch2rename += pipeQ.pipeLine.size();

  nReplayInst.sample(fetch2rename + ROB.size(), target->has_stats());
}
/* }}} */

void OoOProcessor::dumpROB()
// {{{1 Dump rob statistics
{
  uint32_t size = ROB.size();
  fmt::print("ROB: ({})\n", size);

  for (uint32_t i = 0; i < size; i++) {
    uint32_t pos = ROB.getIDFromTop(i);

    Dinst *dinst = ROB.getData(pos);
    dinst->dump("");
  }

  size = rROB.size();
  fmt::print("rROB: ({})\n", size);
  for (uint32_t i = 0; i < size; i++) {
    uint32_t pos = rROB.getIDFromTop(i);

    Dinst *dinst = rROB.getData(pos);
    if (dinst->isReplay()) {
      fmt::print("-----REPLAY--------\n");
    }
    dinst->dump("");
  }
}

bool OoOProcessor::loadIsSpec() {
  std::vector<double> mem_unresolved;
  std::vector<double> br_unresolved;
  std::vector<double> div_unresolved;

  auto robSize = ROB.size();
  if (robSize > 0) {
    for (uint32_t i = 0u; i < robSize; i++) {
      uint32_t pos   = ROB.getIDFromTop(i);
      Dinst   *dinst = ROB.getData(pos);
      if (dinst->getInst()->isMemory()) {
        if (!dinst->isExecuting()) {
          mem_unresolved.push_back(pos);
          // fprintf(stderr,"Mem is at Unresolved and vector_value is (%f)\n",  mem_unresolved.size());
        }
      } else if (dinst->getInst()->isBranch()) {
        if (!dinst->isExecuted()) {
          br_unresolved.push_back(pos);
        }
      } else if (dinst->getInst()->getOpcode() == Opcode::iCALU_FPDIV || dinst->getInst()->getOpcode() == Opcode::iCALU_DIV) {
        if (!dinst->isExecuted()) {
          div_unresolved.push_back(pos);
        }
      }
    }
  }

  if (mem_unresolved.size() > 0 || br_unresolved.size() > 0 || div_unresolved.size() > 0) {
    return true;
  } else {
    return false;
  }
}

// 1}}}
