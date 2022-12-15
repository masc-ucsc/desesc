// See LICENSE for details.

#include "InOrderProcessor.h"

#include "ClusterManager.h"
#include "FetchEngine.h"
#include "GMemorySystem.h"
#include "config.hpp"

InOrderProcessor::InOrderProcessor(GMemorySystem *gm, CPU_t i)
    : GProcessor(gm, i)
    , RetireDelay(Config::get_integer("soc", "core", i, "retire_delay"))
    , lsq(i, 32768)
    , clusterManager(gm, i, this) {  // {{{1

  auto smtnum = get_smt_size();
  RAT         = new Dinst *[LREG_MAX * smtnum * 128];
  bzero(RAT, sizeof(Dinst *) * LREG_MAX * smtnum * 128);
}
// 1}}}

InOrderProcessor::~InOrderProcessor() { /*{{{*/
  delete RAT;
  // Nothing to do
} /*}}}*/

bool InOrderProcessor::advance_clock_drain() {
  bool abort = decode_stage();
  if (abort || !busy) {
    return busy;
  }

  // RENAME Stage
  if (!pipeQ.instQueue.empty()) {
    uint32_t n_insn = issue();
    spaceInInstQueue += n_insn;
  } else if (ROB.empty() && rROB.empty()) {
    // Still busy if we have some in-flight requests
    busy = pipeQ.pipeLine.hasOutstandingItems();
  }

  retire();

  return busy;
}

bool InOrderProcessor::advance_clock() {
  I(is_power_up());

  fetch();

  return advance_clock_drain();
}

void InOrderProcessor::executing(Dinst *dinst) { (void)dinst; }

void InOrderProcessor::executed(Dinst *dinst) { (void)dinst; }

StallCause InOrderProcessor::add_inst(Dinst *dinst) { /*{{{*/

  const Instruction *inst = dinst->getInst();

  size_t rat_off = dinst->getFlowId() % get_smt_size();

#if 1
#if 0
  // Simple in-order
  if(((RAT[inst->getSrc1()+rat_off] != 0) && (inst->getSrc1() != LREG_NoDependence) && (inst->getSrc1() != LREG_InvalidOutput)) ||
     ((RAT[inst->getSrc2()+rat_off] != 0) && (inst->getSrc2() != LREG_NoDependence) && (inst->getSrc2() != LREG_InvalidOutput))||
     ((RAT[inst->getDst1()+rat_off] != 0) && (inst->getDst1() != LREG_InvalidOutput))||
     ((RAT[inst->getDst2()+rat_off] != 0) && (inst->getDst2() != LREG_InvalidOutput))) {
#else
#if 1
  // Simple in-order for RAW, but not WAW or WAR
  if (((RAT[inst->getSrc1() + rat_off] != 0) && (inst->getSrc1() != LREG_NoDependence))
      || ((RAT[inst->getSrc2() + rat_off] != 0) && (inst->getSrc2() != LREG_NoDependence))) {
#else
  // scoreboard, no output dependence
  if (((RAT[inst->getDst1() + rat_off] != 0) && (inst->getDst1() != LREG_InvalidOutput))
      || ((RAT[inst->getDst2() + rat_off] != 0) && (inst->getDst2() != LREG_InvalidOutput))) {
#endif
#endif

#if 0
    //Useful for debug
    if (hid == 1 ){
      fmt::print("\n-------------------------\n");
      string str ="";
      str.append("\nCONFLICT->");
      if (RAT[inst->getSrc1()] != 0){
        str.append("src1, ");
        fmt::print(" SRC1 = {}, RAT[entry] = {}\n",inst->getSrc1(), RAT[inst->getSrc1()] );
        RAT[inst->getSrc1()]->dump("\nSRC1 in use by:");
      }

      if (RAT[inst->getSrc2()] != 0){
        str.append("src2, ");
        RAT[inst->getSrc2()]->dump("\nSRC2 in use by:");
      }

      if ((RAT[inst->getDst1()] != 0) && (inst->getDst2() != LREG_InvalidOutput)){
        str.append("dst1, ");
        RAT[inst->getDst1()]->dump("\nDST1 in use by:");
      }

      if ((RAT[inst->getDst2()] != 0) && (inst->getDst2() != LREG_InvalidOutput)){
        str.append("dst2, ");
        RAT[inst->getDst2()]->dump("\nDST2 in use by:");
      }

      dinst->dump(str.c_str());

    }
#endif
  return SmallWinStall;
}
#endif

if ((ROB.size() + rROB.size()) >= (MaxROBSize - 1)) {
  return SmallROBStall;
}

Cluster *cluster = dinst->getCluster();
if (!cluster) {
  Resource *res = clusterManager.getResource(dinst);
  cluster       = res->getCluster();
  dinst->setCluster(cluster, res);
}

I(dinst->getFlowId() == hid);

StallCause sc = cluster->canIssue(dinst);
if (sc != NoStall) {
  return sc;
}

// FIXME: rafactor the rest of the function that it is the same as in OoOProcessor (share same function in GPRocessor)

// BEGIN INSERTION (note that cluster already inserted in the window)
// dinst->dump("");

nInst[inst->getOpcode()]->inc(dinst->has_stats());  // FIXME: move to cluster

ROB.push(dinst);

if (!dinst->isSrc2Ready()) {
  // It already has a src2 dep. It means that it is solved at
  // retirement (Memory consistency. coherence issues)
  if (RAT[inst->getSrc1() + rat_off]) {
    RAT[inst->getSrc1() + rat_off]->addSrc1(dinst);
  }
} else {
  if (RAT[inst->getSrc1() + rat_off]) {
    RAT[inst->getSrc1() + rat_off]->addSrc1(dinst);
  }

  if (RAT[inst->getSrc2() + rat_off]) {
    RAT[inst->getSrc2() + rat_off]->addSrc2(dinst);
  }
}

I(!dinst->isExecuted());

dinst->setRAT1Entry(&RAT[inst->getDst1() + rat_off]);
dinst->setRAT2Entry(&RAT[inst->getDst2() + rat_off]);

dinst->getCluster()->add_inst(dinst);

RAT[inst->getDst1() + rat_off] = dinst;
RAT[inst->getDst2() + rat_off] = dinst;

I(dinst->getCluster());
dinst->markRenamed();

return NoStall;
} /*}}}*/

void InOrderProcessor::retire() { /*{{{*/

  // Pass all the ready instructions to the rrob
  bool stats = false;
  while (!ROB.empty()) {
    Dinst *dinst = ROB.top();
    stats        = dinst->has_stats();

    I(hid == dinst->getFlowId());

    bool done = dinst->getClusterResource()->preretire(dinst, false);
    if (!done) {
      break;
    }

    rROB.push(dinst);
    ROB.pop();

    nCommitted.inc(dinst->has_stats());
  }

  robUsed.sample(ROB.size(), stats);
  rrobUsed.sample(rROB.size(), stats);

  for (uint16_t i = 0; i < RetireWidth && !rROB.empty(); i++) {
    Dinst *dinst = rROB.top();

    if (!dinst->isExecuted()) {
      break;
    }

    if ((dinst->getExecutedTime() + RetireDelay) >= globalClock) {
      break;
    }

    I(dinst->getCluster());

    bool done = dinst->getCluster()->retire(dinst, false);
    if (!done) {
      return;
    }

#ifndef NDEBUG
    if (!dinst->getInst()->isStore()) {  // Stores can perform after retirement
      I(dinst->isPerformed());
    }
#endif

    if (dinst->isPerformed()) {  // Stores can perform after retirement
      dinst->destroy();
    }
    rROB.pop();
  }

} /*}}}*/

void InOrderProcessor::replay(Dinst *dinst) { /*{{{*/

  // FIXME: foo should be equal to the number of in-flight instructions (check OoOProcessor)
  size_t foo = 1;
  nReplayInst.sample(foo, dinst->has_stats());

  // FIXME: How do we manage a replay in this processor??
} /*}}}*/
