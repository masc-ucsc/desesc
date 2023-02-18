// See LICENSE for details.

#pragma once

#include "estl.hpp"
#include "memory_system.hpp"
#include "stats.hpp"

class Nice_cache : public MemObj {
private:
  // a 100% hit cache, used for debugging or as main memory
  const uint32_t hitDelay;
  const uint32_t bsize;
  const uint32_t bsizeLog2;

  const bool cold_misses;

  HASH_SET<uint32_t> warmup;
  uint32_t           warmupStepStart;
  uint32_t           warmupStep;
  uint32_t           warmupNext;
  uint32_t           warmupSlowEvery;

protected:
  // BEGIN Statistics
  Stats_cntr readHit;
  Stats_cntr pushDownHit;
  Stats_cntr writeHit;

  // The following statistics don't make any sense for a niceCache, but are instantiated
  // for compatibility, and to supress bogus warnings from the PowerModel about missing
  // statistics for the Nice_cache.

  Stats_cntr readMiss;
  Stats_cntr readHalfMiss;
  Stats_cntr writeMiss;
  Stats_cntr writeHalfMiss;
  Stats_cntr writeExclusive;
  Stats_cntr writeBack;
  Stats_avg  avgMemLat;

public:
  Nice_cache(Memory_system *gms, const std::string &section, const std::string &name);

  // Entry points to schedule that may schedule a do?? if needed
  void req(MemRequest *req) { doReq(req); };
  void reqAck(MemRequest *req) { doReqAck(req); };
  void setState(MemRequest *req) { doSetState(req); };
  void setStateAck(MemRequest *req) { doSetStateAck(req); };
  void disp(MemRequest *req) { doDisp(req); }

  // This do the real work
  void doReq(MemRequest *r);
  void doReqAck(MemRequest *req);
  void doSetState(MemRequest *req);
  void doSetStateAck(MemRequest *req);
  void doDisp(MemRequest *req);

  void tryPrefetch(Addr_t addr, bool doStats, int degree, Addr_t pref_sign, Addr_t pc, CallbackBase *cb = 0);

  TimeDelta_t ffread(Addr_t addr);
  TimeDelta_t ffwrite(Addr_t addr);

  bool isBusy(Addr_t addr) const;
};
