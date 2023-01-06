// See LICENSE for details.

#pragma once

#include "fetchengine.hpp"
#include "gprocessor.hpp"
#include "lsq.hpp"
#include "pipeline.hpp"
#include "iassert.hpp"

class GPUSMProcessor : public GProcessor {
private:
  uint32_t numSP;

  LSQNone lsq;

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
