// See LICENSE for details.

#include "nice_cache.hpp"

#include "config.hpp"
#include "memory_system.hpp"
#include "memrequest.hpp"

Nice_cache::Nice_cache(Memory_system *gms, const std::string &sec, const std::string &n)
    : MemObj(sec, n)
    , hitDelay(Config::get_integer(sec, "delay"))
    , bsize(Config::get_power2(sec, "line_size"))
    , bsizeLog2(log2i(Config::get_power2(sec, "line_size")))
    , cold_misses(Config::get_bool(sec, "cold_misses"))
    , readHit(fmt::format("{}:readHit", n))
    , pushDownHit(fmt::format("{}:pushDownHit", n))
    , writeHit(fmt::format("{}:writeHit", n))
    , readMiss(fmt::format("{}:readMiss", n))
    , readHalfMiss(fmt::format("{}:readHalfMiss", n))
    , writeMiss(fmt::format("{}:writeMiss", n))
    , writeHalfMiss(fmt::format("{}:writeHalfMiss", n))
    , writeExclusive(fmt::format("{}:writeExclusive", n))
    , writeBack(fmt::format("{}:writeBack", n))
    , avgMemLat(fmt::format("{}_avgMemLat", n)) {
  (void)gms;  // nice cache does not have lower level
  warmupStepStart = 256 / 4;
  warmupStep      = warmupStepStart;
  warmupNext      = 16;
  warmupSlowEvery = 16;
}

void Nice_cache::doReq(MemRequest *mreq) {
  TimeDelta_t hdelay = hitDelay;

  if (mreq->isWarmup()) {
    hdelay = 1;
  }

  readHit.inc(mreq->has_stats());

  if (!cold_misses && warmup.find(mreq->getAddr() >> bsizeLog2) == warmup.end()) {
    warmup.insert(mreq->getAddr() >> bsizeLog2);
    hdelay = 1;
  }

  if (mreq->isHomeNode()) {
    mreq->ack(hdelay);
    return;
  }
  if (mreq->getAction() == ma_setValid || mreq->getAction() == ma_setExclusive) {
    mreq->convert2ReqAck(ma_setExclusive);
  } else {
    mreq->convert2ReqAck(ma_setDirty);
  }

  avgMemLat.sample(hdelay, mreq->has_stats());
  readHit.inc(mreq->has_stats());
  router->scheduleReqAck(mreq, hdelay);
}

void Nice_cache::doReqAck(MemRequest *req) {
  (void)req;
  I(0);
}

void Nice_cache::doSetState(MemRequest *req) {
  (void)req;
  I(0);
}

void Nice_cache::doSetStateAck(MemRequest *req) {
  (void)req;
  I(0);
}

void Nice_cache::doDisp(MemRequest *mreq) {
  writeHit.inc(mreq->has_stats());
  mreq->ack(hitDelay);
}

bool Nice_cache::isBusy(Addr_t addr) const {
  (void)addr;
  return false;
}

void Nice_cache::tryPrefetch(Addr_t addr, bool doStats, int degree, Addr_t pref_sign, Addr_t pc, CallbackBase *cb) {
  (void)addr;
  (void)doStats;
  (void)degree;
  (void)pref_sign;
  (void)pc;
  if (cb) {
    cb->destroy();
  }
}

TimeDelta_t Nice_cache::ffread(Addr_t addr) {
  (void)addr;
  return 1;
}

TimeDelta_t Nice_cache::ffwrite(Addr_t addr) {
  (void)addr;
  return 1;
}
