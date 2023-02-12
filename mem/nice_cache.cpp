// See LICENSE for details.

#include "nice_cache.hpp"
#include "memrequest.hpp"
#include "memory_system.hpp"
#include "config.hpp"

NICECache::NICECache(MemorySystem *gms, const std::string &section, const std::string &sName)
    /* dummy constructor {{{1 */
    : MemObj(section, sName)
    , hitDelay(Config::get_integer(section, "delay"))
    , bsize(Config::get_power2(section, "line_size"))
    , bsizeLog2(log2i(Config::get_power2(section, "line_size")))
    , cold_misses(Config::get_bool(section, "cold_misses"))
    , readHit("%s:readHit", sName)
    , pushDownHit("%s:pushDownHit", sName)
    , writeHit("%s:writeHit", sName)
    , readMiss("%s:readMiss", sName)
    , readHalfMiss("%s:readHalfMiss", sName)
    , writeMiss("%s:writeMiss", sName)
    , writeHalfMiss("%s:writeHalfMiss", sName)
    , writeExclusive("%s:writeExclusive", sName)
    , writeBack("%s:writeBack", sName)
    , avgMemLat("%s_avgMemLat", sName) {
  // FIXME: the hitdelay should be converted to dyn_hitDelay to support DVFS

  warmupStepStart = 256 / 4;
  warmupStep      = warmupStepStart;
  warmupNext      = 16;
  warmupSlowEvery = 16;
}
/* }}} */

void NICECache::doReq(MemRequest *mreq)
/* read (down) {{{1 */
{
  TimeDelta_t hdelay = hitDelay;

  if (mreq->isWarmup()) {
    hdelay = 1;
  }

  readHit.inc(mreq->getStatsFlag());

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

  avgMemLat.sample(hdelay, mreq->getStatsFlag());
  readHit.inc(mreq->getStatsFlag());
  router->scheduleReqAck(mreq, hdelay);
}

void NICECache::doReqAck(MemRequest *req)
/* req ack {{{1 */
{
  I(0);
}
// 1}}}

void NICECache::doSetState(MemRequest *req)
/* change state request  (up) {{{1 */
{
  I(0);
}
/* }}} */

void NICECache::doSetStateAck(MemRequest *req)
/* push (down) {{{1 */
{
  I(0);
}
/* }}} */

void NICECache::doDisp(MemRequest *mreq)
/* push (up) {{{1 */
{
  writeHit.inc(mreq->getStatsFlag());
  mreq->ack(hitDelay);
}
/* }}} */

bool NICECache::isBusy(Addr_t addr) const
/* can accept reads? {{{1 */
{
  return false;
}
/* }}} */

void NICECache::tryPrefetch(Addr_t addr, bool doStats, int degree, Addr_t pref_sign, Addr_t pc, CallbackBase *cb)
/* drop prefetch {{{1 */
{
  if (cb) {
    cb->destroy();
  }
}
/* }}} */

TimeDelta_t NICECache::ffread(Addr_t addr)
/* warmup fast forward read {{{1 */
{
  return 1;
}
/* }}} */

TimeDelta_t NICECache::ffwrite(Addr_t addr)
/* warmup fast forward writed {{{1 */
{
  return 1;
}
/* }}} */
