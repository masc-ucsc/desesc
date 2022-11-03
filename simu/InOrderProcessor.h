// See LICENSE for details.

#pragma once

#include "FetchEngine.h"
#include "GProcessor.h"
#include "LSQ.h"
#include "Pipeline.h"
#include "iassert.hpp"

struct SMTFetch {
  FetchEngine *fe;
  Time_t       smt_lastTime;
  int          smt_cnt;
  int          smt_active;
  int          smt_turn;

  SMTFetch() {
    fe           = 0;
    smt_lastTime = 0;
    smt_cnt      = 1;
    smt_active   = 1;
    smt_turn     = 0;
  };

  bool update(bool space);
};
class InOrderProcessor : public GProcessor {
private:
  const int32_t RetireDelay;

  FetchEngine *ifid;
  PipeQueue    pipeQ;
  int32_t      spaceInInstQueue;

  LSQNone lsq;
  bool    busy;
  bool    lastrob_getStatsFlag;

  SMTFetch *sf;

  // Dinst *RAT[LREG_MAX];
  Dinst **RAT;

  void fetch(FlowID fid);

protected:
  ClusterManager clusterManager;
  // BEGIN VIRTUAL FUNCTIONS of GProcessor

  bool advance_clock(FlowID fid);
  void retire();

  StallCause addInst(Dinst *dinst);
  // END VIRTUAL FUNCTIONS of GProcessor

public:
  InOrderProcessor(GMemorySystem *gm, CPU_t i);
  virtual ~InOrderProcessor();

  void executing(Dinst *dinst);
  void executed(Dinst *dinst);
  LSQ *getLSQ() { return &lsq; }
  void replay(Dinst *dinst);
  bool isFlushing() {
    I(0);
    return false;
  }
  bool isReplayRecovering() {
    I(0);
    return false;
  }
  Time_t getReplayID() {
    I(0);
    return false;
  }
};
