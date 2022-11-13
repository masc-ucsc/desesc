// See LICENSE for details.

#include "InOrderProcessor.h"

#include "ClusterManager.h"
#include "FetchEngine.h"
#include "GMemorySystem.h"
#include "TaskHandler.h"
#include "config.hpp"
#include "estl.h"

InOrderProcessor::InOrderProcessor(GMemorySystem *gm, CPU_t i)
    : GProcessor(gm, i)
    , RetireDelay(Config::get_integer("soc", "core", i , "retire_delay"))
    , pipeQ(i)
    , lsq(i, 32768)
    , clusterManager(gm, i, this) {  // {{{1

  auto fName = fmt::format("fetch({})", smt_ctx);

  auto it = fetch_map.find(fName);
  if (it != fetch_map.end() && smt > 1) {
    ifid = new FetchEngine(i, gm, it->second->fe);
    sf   = it->second;
  } else {
    ifid            = new FetchEngine(i, gm);
    sf              = std::make_shared<SMTFetch>();
    sf->fe          = ifid;
    fetch_map[fName] = sf;
  }

  spaceInInstQueue = InstQueueSize;

  uint32_t smtnum = 1;  // getMaxFlows();
  RAT             = new Dinst *[LREG_MAX * smtnum * 128];
  bzero(RAT, sizeof(Dinst *) * LREG_MAX * smtnum * 128);

  busy                 = false;
  lastrob_getStatsFlag = false;
}
// 1}}}

InOrderProcessor::~InOrderProcessor() { /*{{{*/
  delete RAT;
  // Nothing to do
} /*}}}*/

bool SMTFetch::update(bool space) {
  // {{{1
  if (smt_lastTime != globalClock) {
    smt_lastTime = globalClock;
    smt_active   = smt_cnt;
    smt_cnt      = 1;
  } else {
    smt_cnt++;
  }
  I(smt_active > 0);

  smt_turn--;
  if (smt_turn < 0 && space) {
    if (smt_cnt == smt_active)
      smt_turn = 0;
    else
      smt_turn = smt_active;
    return true;
  }

  return false;
}  // 1}}}

void InOrderProcessor::fetch() { /*{{{*/
  // TODO: Move this to GProcessor (same as in OoOProcessor)
  I(eint);
  I(is_power_up());

  if (smt > 1) {
    bool run = sf->update(spaceInInstQueue >= FetchWidth);
    if (!run)
      return;
  }

  if (ifid->isBlocked())
    return;

  auto smt_hid = hid; // FIXME: do SMT fetch
  IBucket *bucket = pipeQ.pipeLine.newItem();
  if (bucket) {
    ifid->fetch(bucket, eint, smt_hid);
  }

} /*}}}*/

bool InOrderProcessor::advance_clock_drain() {
  bool getStatsFlag = lastrob_getStatsFlag;
  if (!ROB.empty()) {
    getStatsFlag         = ROB.top()->getStatsFlag();
    lastrob_getStatsFlag = getStatsFlag;
  }

  adjust_clock(getStatsFlag);

  // ID Stage (insert to instQueue)
  if (spaceInInstQueue >= FetchWidth) {
    IBucket *bucket = pipeQ.pipeLine.nextItem();
    if (bucket) {
      I(!bucket->empty());
      spaceInInstQueue -= bucket->size();
      pipeQ.instQueue.push(bucket);
    } else {
      noFetch2.inc(getStatsFlag);
    }
  } else {
    noFetch.inc(getStatsFlag);
  }

  busy = true;
  // RENAME Stage
  if (!pipeQ.instQueue.empty()) {
    uint32_t n_insn = issue(pipeQ);
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
  Hartid_t rat_off = 0;  // no need, add_inst is private per thread. Cluster is shared (dinst->getFlowId() % getMaxFlows())*LREG_MAX;

#if 1
#if 0
  // Simple in-order
  if(((RAT[inst->getSrc1()] != 0) && (inst->getSrc1() != LREG_NoDependence) && (inst->getSrc1() != LREG_InvalidOutput)) ||
     ((RAT[inst->getSrc2()] != 0) && (inst->getSrc2() != LREG_NoDependence) && (inst->getSrc2() != LREG_InvalidOutput))||
     ((RAT[inst->getDst1()] != 0) && (inst->getDst1() != LREG_InvalidOutput))||
     ((RAT[inst->getDst2()] != 0) && (inst->getDst2() != LREG_InvalidOutput))) {
#else
#if 1
  // Simple in-order for RAW, but not WAW or WAR
  if (((RAT[inst->getSrc1() + rat_off] != 0) && (inst->getSrc1() != LREG_NoDependence))
      || ((RAT[inst->getSrc2() + rat_off] != 0) && (inst->getSrc2() != LREG_NoDependence))) {
#else
                       // scoreboard, no output dependence
  if (((RAT[inst->getDst1()] != 0) && (inst->getDst1() != LREG_InvalidOutput))
      || ((RAT[inst->getDst2()] != 0) && (inst->getDst2() != LREG_InvalidOutput))) {
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

if ((ROB.size() + rROB.size()) >= (MaxROBSize - 1))
  return SmallROBStall;

Cluster *cluster = dinst->getCluster();
if (!cluster) {
  Resource *res = clusterManager.getResource(dinst);
  cluster       = res->getCluster();
  dinst->setCluster(cluster, res);
}

I(dinst->getFlowId() == hid);

StallCause sc = cluster->canIssue(dinst);
if (sc != NoStall)
  return sc;

// FIXME: rafactor the rest of the function that it is the same as in OoOProcessor (share same function in GPRocessor)

// BEGIN INSERTION (note that cluster already inserted in the window)
// dinst->dump("");

nInst[inst->getOpcode()]->inc(dinst->getStatsFlag());  // FIXME: move to cluster

ROB.push(dinst);

if (!dinst->isSrc2Ready()) {
  // It already has a src2 dep. It means that it is solved at
  // retirement (Memory consistency. coherence issues)
  if (RAT[inst->getSrc1() + rat_off])
    RAT[inst->getSrc1() + rat_off]->addSrc1(dinst);
} else {
  if (RAT[inst->getSrc1() + rat_off])
    RAT[inst->getSrc1() + rat_off]->addSrc1(dinst);

  if (RAT[inst->getSrc2() + rat_off])
    RAT[inst->getSrc2() + rat_off]->addSrc2(dinst);
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
    stats        = dinst->getStatsFlag();

    I(hid == dinst->getFlowId());

    bool done = dinst->getClusterResource()->preretire(dinst, false);
    if (!done)
      break;

    rROB.push(dinst);
    ROB.pop();

    nCommitted.inc(dinst->getStatsFlag());
  }

  robUsed.sample(ROB.size(), stats);
  rrobUsed.sample(rROB.size(), stats);

  for (uint16_t i = 0; i < RetireWidth && !rROB.empty(); i++) {
    Dinst *dinst = rROB.top();

    if (!dinst->isExecuted())
      break;

    if ((dinst->getExecutedTime() + RetireDelay) >= globalClock)
      break;

    I(dinst->getCluster());

    bool done = dinst->getCluster()->retire(dinst, false);
    if (!done)
      return;

#ifndef NDEBUG
    if (!dinst->getInst()->isStore())  // Stores can perform after retirement
      I(dinst->isPerformed());
#endif

    if (dinst->isPerformed()) { // Stores can perform after retirement
      dinst->destroy();
    }
    rROB.pop();
  }

} /*}}}*/

void InOrderProcessor::replay(Dinst *dinst) { /*{{{*/

  // FIXME: foo should be equal to the number of in-flight instructions (check OoOProcessor)
  size_t foo = 1;
  nReplayInst.sample(foo, dinst->getStatsFlag());

  // FIXME: How do we manage a replay in this processor??
} /*}}}*/
