// See LICENSE for details.

#pragma once

#include "iassert.hpp"
#include "callback.hpp"

#include "FastQueue.h"
#include "FetchEngine.h"
#include "GOoOProcessor.h"
#include "GStats.h"
#include "Pipeline.h"

class AccProcessor : public GProcessor {
private:
  bool busy;

protected:
  // BEGIN VIRTUAL FUNCTIONS of GProcessor
  bool advance_clock(FlowID fid);

  // Not needed for Acc
  StallCause addInst(Dinst *dinst);
  void       retire();
  void       fetch(FlowID fid);
  LSQ *      getLSQ();
  bool       isFlushing();
  bool       isReplayRecovering();
  Time_t     getReplayID();
  void       executing(Dinst *dinst);
  void       executed(Dinst *dinst);

  virtual void replay(Dinst *target){}; // = 0;

  // END VIRTUAL FUNCTIONS of GProcessor

  AddrType myAddr;
  AddrType addrIncr;
  int      reqid;
  int      total_accesses;
  int      outstanding_accesses;

  GStatsCntr accReads;
  GStatsCntr accWrites;

  GStatsAvg accReadLatency;
  GStatsAvg accWriteLatency;

  void read_performed(uint32_t id, Time_t startTime);
  void write_performed(uint32_t id, Time_t startTime);
  typedef CallbackMember2<AccProcessor, uint32_t, Time_t, &AccProcessor::read_performed>  read_performedCB;
  typedef CallbackMember2<AccProcessor, uint32_t, Time_t, &AccProcessor::write_performed> write_performedCB;

public:
  AccProcessor(GMemorySystem *gm, CPU_t i);
  virtual ~AccProcessor();
};

