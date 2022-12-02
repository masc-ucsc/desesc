// See LICENSE for details.

#pragma once

#include "FetchEngine.h"
#include "GProcessor.h"
#include "LSQ.h"
#include "Pipeline.h"
#include "nanassert.h"

class GPUSMProcessor : public GProcessor {
private:
  FetchEngine IFID;

  uint32_t numSP;

  LSQNone lsq;
  bool    busy;

  bool *inst_perpe_percyc;  // Only needed for the GPUSMProc

  Dinst **RAT;

  void fetch(Hartid_t fid);

protected:
  ClusterManager clusterManager;
  // BEGIN VIRTUAL FUNCTIONS of GProcessor
  bool advance_clock_drain() override final;
  bool advance_clock() override final;

  StallCause add_inst(Dinst *dinst) override final;

  void retire();
  // END VIRTUAL FUNCTIONS of GProcessor

public:
  GPUSMProcessor(GMemorySystem *gm, CPU_t i);
  virtual ~GPUSMProcessor();

  LSQ *getLSQ() { return &lsq; }
  void replay(Dinst *dinst);

  bool is_nuking() override final { return false; }

  bool isReplayRecovering() {
    I(0);
    return false;
  }

  Time_t getReplayID() {
    I(0);
    return false;
  }

  std::string get_type() const final { return "gpu"; }
};
