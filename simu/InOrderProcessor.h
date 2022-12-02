// See LICENSE for details.

#pragma once

#include "FetchEngine.h"
#include "GProcessor.h"
#include "LSQ.h"
#include "Pipeline.h"
#include "absl/container/flat_hash_map.h"
#include "iassert.hpp"

class InOrderProcessor : public GProcessor {
private:
  const int32_t RetireDelay;

  LSQNone lsq;
  bool    busy;

  Dinst **RAT;

  void fetch();

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
