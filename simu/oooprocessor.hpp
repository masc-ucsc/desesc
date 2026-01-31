// See LICENSE for details.

#pragma once

#include <algorithm>
#include <vector>

#include "callback.hpp"
#include "fastqueue.hpp"
#include "fetchengine.hpp"
#include "gprocessor.hpp"
#include "iassert.hpp"
#include "pipeline.hpp"
#include "stats.hpp"
#include "stats_code.hpp"

// #define TRACK_FORWARDING 1
#define TRACK_TIMELEAK 1
#define DEP_LIST_SIZE  64

// #define BTT_SIZE 512 //16 //512
#define NUM_LOADS               6  // 32 //32 //6 //6 //16 // maximum number of loads trackable by LDBP framework
#define NUM_OPS                 6  // 32 //4 //8 //16 // maximum number of operations between LD and BR in code snippet
#define BTT_MAX_ACCURACY        7
#define MAX_POWER_SAVE_MODE_CTR 100000

class OoOProcessor : public GProcessor {
private:
  class RetireState {
  public:
    Time_t r_dinst_ID;
    Time_t dinst_ID;
    Dinst* r_dinst;
    Dinst* dinst;
    bool   operator==(const RetireState& a) const { return a.dinst_ID == dinst_ID || a.r_dinst_ID == r_dinst_ID; };
    RetireState() {
      r_dinst_ID = 0;
      dinst_ID   = 0;
      r_dinst    = 0;
      dinst      = 0;
    }
  };

  const bool    MemoryReplay;
  const int32_t RetireDelay;

  LSQFull lsq;

  uint32_t serialize_level;
  uint32_t serialize;
  int32_t  serialize_for;
  uint32_t forwardProg_threshold;
  Dinst*   last_serialized;
  Dinst*   last_serializedST;

  RegType_array<Dinst*> RAT;
  RegType_array<Dinst*> TRAT;
  int32_t               nTotalRegs;

  RegType_array<Dinst*> serializeRAT;
  RegType               last_serializeLogical;
  Addr_t                last_serializePC;

  bool   replayRecovering;
  Time_t replayID;
  bool   flushing;

  Hartid_t flushing_fid;

  RetireState                                                           last_state;
  void                                                                  retire_lock_check();
  bool                                                                  scooreMemory;
  StaticCallbackMember0<OoOProcessor, &OoOProcessor::retire_lock_check> retire_lock_checkCB;

protected:
  ClusterManager clusterManager;

#ifdef TRACK_TIMELEAK
  Stats_avg  avgPNRHitLoadSpec;
  Stats_hist avgPNRMissLoadSpec;
#endif

  Stats_code codeProfile;
  double     codeProfile_trigger;

  // BEGIN VIRTUAL FUNCTIONS of GProcessor
  bool advance_clock_drain() override final;
  bool advance_clock() override final;

  StallCause add_inst(Dinst* dinst) override final;
  void       retire();

  // END VIRTUAL FUNCTIONS of GProcessor
public:
  OoOProcessor(std::shared_ptr<Gmemory_system> gm, CPU_t i);
  virtual ~OoOProcessor();

  void   executing(Dinst* dinst) override final;
  void   executed(Dinst* dinst) override final;
  void   flushed(Dinst* dinst) override final;
  void   try_flush(Dinst* dinst) override final;
  LSQ*   getLSQ() override final { return &lsq; }
  void   replay(Dinst* target) override final;
  bool   is_nuking() override final { return flushing; }
  bool   isReplayRecovering() override final { return replayRecovering; }
  Time_t getReplayID() override final { return replayID; }

  void dump_rat() {
    // auto rat_max= static_cast <int> ( RegType::LREG_MAX);

    auto    rat_max = RegType::LREG_FP31;
    RegType i       = RegType::LREG_R0;
    int     n       = 0;
    while (i <= rat_max) {
      {
        // auto reg = static_cast <RegType> (i);
        // RegType reg = static_cast <RegType> (i);
        if (RAT[i]) {
          std::cout << "RATENTRY: " << RAT[i]->getID() << std::endl;
          bool pend = RAT[i]->hasPending();
          std::cout << "RATENTRY: " << RAT[i]->getID() << "has Pending :is " << pend << std::endl;
        }

        n++;
        i = (RegType)n;
        // break;
      }
    }
  }

  void dumpROB();
  bool loadIsSpec();

  bool isSerializing() const { return serialize_for != 0; }

  std::string get_type() const final { return "ooo"; }
};
