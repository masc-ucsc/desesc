// See LICENSE for details.

#pragma once

#include "absl/container/flat_hash_map.h"

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

  std::shared_ptr<SMTFetch> sf;

  // Dinst *RAT[LREG_MAX];
  Dinst **RAT;

  void fetch();

  static inline absl::flat_hash_map<std::string, std::shared_ptr<SMTFetch>> fetch_map;

protected:
  ClusterManager clusterManager;

  // BEGIN VIRTUAL FUNCTIONS of GProcessor
  bool advance_clock_drain() override final;
  bool advance_clock() override final;

  void retire();

  StallCause add_inst(Dinst *dinst) override final;
  // END VIRTUAL FUNCTIONS of GProcessor

public:
  InOrderProcessor(GMemorySystem *gm, CPU_t i);
  virtual ~InOrderProcessor();

  void executing(Dinst *dinst) override final;
  void executed(Dinst *dinst) override final;

  // No LSQ speculation, so not memory replay (just populate accordingly)
  LSQ *getLSQ() override final { return &lsq; }
  void replay(Dinst *dinst) override final;
  bool is_nuking() override final {
    I(0);
    return false;
  }
  bool isReplayRecovering() override final {
    I(0);
    return false;
  }
  Time_t getReplayID() override final {
    I(0);
    return false;
  }

  std::string get_type() const final { return "inorder"; }
};
