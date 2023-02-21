// See LICENSE for details.

#include "ccache.hpp"

#include <climits>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "cache_port.hpp"
#include "config.hpp"
#include "gprocessor.hpp"
#include "iassert.hpp"
#include "memrequest.hpp"
#include "mshr.hpp"

extern "C" uint64_t esesc_mem_read(uint64_t addr);

#ifndef NDEBUG
void CCache::trackAddress(MemRequest *mreq) {
  (void)mreq;
  // I((mreq->getAddr() & 0xFFFF0) != 0x74f00);
  // I(mreq->getID() != 5);
  // I((mreq->getAddr() & 0xFFFF0) != 0x15e10);
}
#endif

#if 0
template<class... Args>
void MTRACE(Args... args) {
  fmt::print(args...);
  fmt::print("\n");
}
    // if (getName() == "DL1(0)") 
#else
#define MTRACE(a...)
#endif

CCache::CCache(Memory_system *gms, const std::string &section, const std::string &name)
    : MemObj(section, name)
    , nTryPrefetch(fmt::format("{}:nTryPrefetch", name))
    , nSendPrefetch(fmt::format("{}:nSendPrefetch", name))
    , displacedSend(fmt::format("{}:displacedSend", name))
    , displacedRecv(fmt::format("{}:displacedRecv", name))
    , invAll(fmt::format("{}:invAll", name))
    , invOne(fmt::format("{}:invOne", name))
    , invNone(fmt::format("{}:invNone", name))
    , writeBack(fmt::format("{}:writeBack", name))
    , lineFill(fmt::format("{}:lineFill", name))
    , avgMissLat(fmt::format("{}_avgMissLat", name))
    , avgMemLat(fmt::format("{}_avgMemLat", name))
    , avgHalfMemLat(fmt::format("{}_avgHalfMemLat", name))
    , avgSnoopLat(fmt::format("{}_avgSnoopLat", name))
    , avgPrefetchLat(fmt::format("{}_avgPrefetchLat", name))
    , capInvalidateHit(fmt::format("{}_capInvalidateHit", name))
    , capInvalidateMiss(fmt::format("{}_capInvalidateMiss", name))
    , invalidateHit(fmt::format("{}_invalidateHit", name))
    , invalidateMiss(fmt::format("{}_invalidateMiss", name))
    , writeExclusive(fmt::format("{}:writeExclusive", name))
    , nPrefetchUseful(fmt::format("{}:nPrefetchUseful", name))
    , nPrefetchWasteful(fmt::format("{}:nPrefetchWasteful", name))
    , nPrefetchLineFill(fmt::format("{}:nPrefetchLineFill", name))
    , nPrefetchRedundant(fmt::format("{}:nPrefetchRedundant", name))
    , nPrefetchHitLine(fmt::format("{}:nPrefetchHitLine", name))
    , nPrefetchHitPending(fmt::format("{}:nPrefetchHitPending", name))
    , nPrefetchHitBusy(fmt::format("{}:nPrefetchHitBusy", name))
    , nPrefetchDropped(fmt::format("{}:nPrefetchDropped", name))
    , cleanupCB(this)
    , port(section, name) {
  s_reqHit[ma_setInvalid]   = new Stats_cntr(fmt::format("{}:setInvalidHit", name));
  s_reqHit[ma_setValid]     = new Stats_cntr(fmt::format("{}:readHit", name));
  s_reqHit[ma_setDirty]     = new Stats_cntr(fmt::format("{}:writeHit", name));
  s_reqHit[ma_setShared]    = new Stats_cntr(fmt::format("{}:setSharedHit", name));
  s_reqHit[ma_setExclusive] = new Stats_cntr(fmt::format("{}:setExclusiveHit", name));
  s_reqHit[ma_MMU]          = new Stats_cntr(fmt::format("{}:MMUHit", name));
  s_reqHit[ma_VPCWU]        = new Stats_cntr(fmt::format("{}:VPCMUHit", name));

  s_reqMissLine[ma_setInvalid]   = new Stats_cntr(fmt::format("{}:setInvalidMiss", name));
  s_reqMissLine[ma_setValid]     = new Stats_cntr(fmt::format("{}:readMiss", name));
  s_reqMissLine[ma_setDirty]     = new Stats_cntr(fmt::format("{}:writeMiss", name));
  s_reqMissLine[ma_setShared]    = new Stats_cntr(fmt::format("{}:setSharedMiss", name));
  s_reqMissLine[ma_setExclusive] = new Stats_cntr(fmt::format("{}:setExclusiveMiss", name));
  s_reqMissLine[ma_MMU]          = new Stats_cntr(fmt::format("{}:MMUMiss", name));
  s_reqMissLine[ma_VPCWU]        = new Stats_cntr(fmt::format("{}:VPCMUMiss", name));

  s_reqMissState[ma_setInvalid]   = new Stats_cntr(fmt::format("{}:setInvalidMissState", name));
  s_reqMissState[ma_setValid]     = new Stats_cntr(fmt::format("{}:readMissState", name));
  s_reqMissState[ma_setDirty]     = new Stats_cntr(fmt::format("{}:writeMissState", name));
  s_reqMissState[ma_setShared]    = new Stats_cntr(fmt::format("{}:setSharedMissState", name));
  s_reqMissState[ma_setExclusive] = new Stats_cntr(fmt::format("{}:setExclusiveMissState", name));
  s_reqMissState[ma_MMU]          = new Stats_cntr(fmt::format("{}:MMUMissState", name));
  s_reqMissState[ma_VPCWU]        = new Stats_cntr(fmt::format("{}:VPCMUMissState", name));

  s_reqHalfMiss[ma_setInvalid]   = new Stats_cntr(fmt::format("{}:setInvalidHalfMiss", name));
  s_reqHalfMiss[ma_setValid]     = new Stats_cntr(fmt::format("{}:readHalfMiss", name));
  s_reqHalfMiss[ma_setDirty]     = new Stats_cntr(fmt::format("{}:writeHalfMiss", name));
  s_reqHalfMiss[ma_setShared]    = new Stats_cntr(fmt::format("{}:setSharedHalfMiss", name));
  s_reqHalfMiss[ma_setExclusive] = new Stats_cntr(fmt::format("{}:setExclusiveHalfMiss", name));
  s_reqHalfMiss[ma_MMU]          = new Stats_cntr(fmt::format("{}:MMUHalfMiss", name));
  s_reqHalfMiss[ma_VPCWU]        = new Stats_cntr(fmt::format("{}:VPCMUHalfMiss", name));

  s_reqAck[ma_setInvalid]   = new Stats_cntr(fmt::format("{}:setInvalidAck", name));
  s_reqAck[ma_setValid]     = new Stats_cntr(fmt::format("{}:setValidAck", name));
  s_reqAck[ma_setDirty]     = new Stats_cntr(fmt::format("{}:setDirtyAck", name));
  s_reqAck[ma_setShared]    = new Stats_cntr(fmt::format("{}:setSharedAck", name));
  s_reqAck[ma_setExclusive] = new Stats_cntr(fmt::format("{}:setExclusiveAck", name));
  s_reqAck[ma_MMU]          = new Stats_cntr(fmt::format("{}:MMUAck", name));
  s_reqAck[ma_VPCWU]        = new Stats_cntr(fmt::format("{}:VPCMUAck", name));

  s_reqSetState[ma_setInvalid]   = new Stats_cntr(fmt::format("{}:setInvalidSetState", name));
  s_reqSetState[ma_setValid]     = new Stats_cntr(fmt::format("{}:setValidSetState", name));
  s_reqSetState[ma_setDirty]     = new Stats_cntr(fmt::format("{}:setDirtySetState", name));
  s_reqSetState[ma_setShared]    = new Stats_cntr(fmt::format("{}:setSharedSetState", name));
  s_reqSetState[ma_setExclusive] = new Stats_cntr(fmt::format("{}:setExclusiveSetState", name));
  s_reqSetState[ma_MMU]          = new Stats_cntr(fmt::format("{}:MMUSetState", name));
  s_reqSetState[ma_VPCWU]        = new Stats_cntr(fmt::format("{}:VPCMUSetState", name));

  if (Config::has_entry(section, "nlp_distance")) {
    nlp_degree   = Config::get_integer(section, "nlp_degree", 0, 16);
    nlp_distance = Config::get_integer(section, "nlp_distance", 1, 1024);
    nlp_stride   = Config::get_integer(section, "nlp_stride", 1, 1024);

    nlp_enabled = nlp_degree != 0;
  } else {
    nlp_degree   = 0;
    nlp_distance = 0;
    nlp_stride   = 1;

    nlp_enabled = false;
  }
  allocateMiss = Config::get_bool(section, "allocate_miss");

  incoherent = !Config::get_bool(section, "coherent");
  if (incoherent) {
    clearNeedsCoherence();
  }

  minMissAddr = ULONG_MAX;
  maxMissAddr = 0;

  victim = Config::get_bool(section, "victim");

  coreCoupledFreq = false;
  // if (coreCoupledFreq) {
  //  BootLoader::getPowerModelPtr()->addTurboCoupledMemory(this);
  //}

  cacheBank    = CacheType::create(section, "", name);
  lineSize     = cacheBank->getLineSize();
  lineSizeBits = log2i(lineSize);

  prefetch_degree = Config::get_integer(section, "prefetch_degree", 0, 32);

  auto mega_lines1K  = Config::get_integer(section, "mega_lines1K", 0, 32);  // number of lines touched in 1K to trigger mega
  prefetch_megaratio = lineSize * mega_lines1K / 1024.0;
  if (prefetch_megaratio == 0 || prefetch_megaratio > 1) {
    prefetch_megaratio = 2.0;  // >1 means never active
  }

  if (!allocateMiss && prefetch_megaratio < 1.0) {
    Config::add_error(fmt::format("{} CCache prefetch_megaratio can be set only for allocateMiss caches", section));
    return;
  }

  {
    int16_t lineSize2 = lineSize;
    if (!allocateMiss) {
      lineSize2 = 64;  // FIXME: Do not hardcode upper level line size
    }
    uint32_t MaxRequests = Config::get_integer(section, "max_requests", 1, 8192);

    mshr  = new MSHR(name, 128 * MaxRequests, lineSize2, MaxRequests);
    pmshr = new MSHR(name + "sp", 128 * MaxRequests, lineSize2, MaxRequests);
  }

  I(getLineSize() < 4096);  // To avoid bank selection conflict (insane CCache line)
  I(gms);

  inclusive = Config::get_bool(section, "inclusive");
  directory = Config::get_bool(section, "directory");
  if (directory && !inclusive) {
    Config::add_error(fmt::format("{} CCache can not have a 'directory' without being 'inclusive'", section));
    return;
  }

  if (Config::has_entry(section, "just_directory")) {
    justDirectory = Config::get_bool(section, "just_directory");
  } else {
    justDirectory = false;
  }
  if (justDirectory && !inclusive) {
    Config::add_error("justDirectory option is only possible with inclusive=true");
    return;
  }
  if (justDirectory && !allocateMiss) {
    Config::add_error("justDirectory option is only possible with allocateMiss=false");
    return;
  }
  if (justDirectory && !directory) {
    Config::add_error("justDirectory option is only possible with directory=true");
    return;
  }
  if (justDirectory && victim) {
    Config::add_error("justDirectory option is only possible with directory=true and no victim");
    return;
  }

  MemObj *lower_level = gms->declareMemoryObj(section, "lower_level");
  if (lower_level) {
    addLowerLevel(lower_level);
  }

  lastUpMsg = 0;
}

CCache::~CCache() { cacheBank->destroy(); }

void CCache::cleanup() {
  int n = cacheBank->getNumLines();

  int nInvalidated = 0;

  for (int i = 0; i < n; i++) {
    Line *l = cacheBank->getPLine(n);
    if (!l->isValid()) {
      continue;
    }

    nInvalidated++;
    l->invalidate();
  }

  cleanupCB.scheduleAbs(globalClock + 1000000);
}

void CCache::dropPrefetch(MemRequest *mreq) {
  I(mreq->isPrefetch());

  nPrefetchDropped.inc(mreq->has_stats());
  mreq->setDropped();
  if (mreq->isHomeNode()) {
    mreq->ack();
  } else {
    router->scheduleReqAck(mreq, 0);
  }
}

void CCache::displaceLine(Addr_t naddr, MemRequest *mreq, Line *l) {
  I(naddr != mreq->getAddr());  // naddr is the displace address, mreq is the trigger
  I(l->isValid());

  bool doStats = mreq->has_stats();

  if (inclusive && !mreq->isTopCoherentNode()) {
    if (directory) {
      if (l->getSharingCount() == 0) {
        invNone.inc(doStats);

        // DONE! Nice directory tracking detected no higher level sharing
        if (l->isPrefetch()) {
          return;  // No notification to lower level if prefetch (avoid overheads)
        }
      } else if (l->getSharingCount() == 1) {
        invOne.inc(doStats);

        MemRequest *inv_req = MemRequest::createSetState(this, this, ma_setInvalid, naddr, doStats);
        trackAddress(inv_req);
        int32_t i = router->sendSetStateOthersPos(l->getFirstSharingPos(), inv_req, ma_setInvalid, inOrderUpMessage());
        if (i == 0) {
          inv_req->ack();
        }
      } else {
        // FIXME: optimize directory for 2 or more
        invAll.inc(doStats);

        MemRequest *inv_req = MemRequest::createSetState(this, this, ma_setInvalid, naddr, doStats);
        trackAddress(inv_req);
        int32_t i = router->sendSetStateAll(inv_req, ma_setInvalid, inOrderUpMessage());
        if (i == 0) {
          inv_req->ack();
        }
      }
    } else {
      invAll.inc(doStats);

      MemRequest *inv_req = MemRequest::createSetState(this, this, ma_setInvalid, naddr, doStats);
      int32_t     i       = router->sendSetStateAll(inv_req, ma_setInvalid, inOrderUpMessage());

      if (i == 0) {
        inv_req->ack();
      }
    }
  } else {
    if (l->isPrefetch()) {
      l->clearSharing();
      return;  // No notification to lower level if prefetch (avoid overheads)
    }
  }

  displacedSend.inc(doStats);

  if (l->needsDisp()) {
    MTRACE("displace 0x%llx dirty", naddr);
    router->sendDirtyDisp(naddr, mreq->has_stats(), 1);
    writeBack.inc(mreq->has_stats());
  } else {
    MTRACE("displace 0x%llx clean", naddr);
    router->sendCleanDisp(naddr, l->isPrefetch(), mreq->has_stats(), 1);
  }

  l->clearSharing();
}

CCache::Line *CCache::allocateLine(Addr_t addr, MemRequest *mreq) {
  Addr_t rpl_addr = 0;
  I(mreq->getAddr() == addr);

  I(cacheBank->findLineDebug(addr) == 0);
  Line *l = cacheBank->fillLine_replace(addr, rpl_addr, mreq->getPC(), mreq->isPrefetch());
  lineFill.inc(mreq->has_stats());

  I(l);  // Ignore lock guarantees to find line

  if (l->isValid()) {
    if (l->isPrefetch() && !mreq->isPrefetch()) {
      nPrefetchWasteful.inc(mreq->has_stats());
    }

    // TODO: add a port for evictions. Schedule the displaceLine accordingly
    displaceLine(rpl_addr, mreq, l);
  }

  l->set(mreq);

  if (mreq->isPrefetch()) {
    nPrefetchLineFill.inc(mreq->has_stats());
  }

#if 1
  if (prefetch_megaratio < 1) {
    static int conta = 0;
    Addr_t     pendAddr[32];
    int        pendAddr_counter = 0;
    if (conta++ > 8) {  // Do not try all the time
      conta = 0;

      Addr_t page_addr = (addr >> 10) << 10;
      int    nHit      = 0;
      int    nMiss     = 0;

      for (int i = 0; i < (1024 >> lineSizeBits); i++) {
        if (!mshr->canIssue(page_addr + lineSize * i * nlp_stride)) {
          nHit++;  // Pending request == hit, line already requested
        } else if (cacheBank->findLineNoEffect(page_addr + lineSize * i * nlp_stride)) {
          nHit++;
        } else {
          if (pendAddr_counter < 32) {
            pendAddr[pendAddr_counter++] = page_addr + lineSize * i * nlp_stride;
          }
          nMiss++;
        }
      }

      double ratio = nHit;
      ratio        = ratio / (double)(nHit + nMiss);
      if (ratio > prefetch_megaratio && ratio < 1.0) {
#if 1
        for (int i = 0; i < pendAddr_counter; i++) {
          tryPrefetch(pendAddr[i], mreq->has_stats(), 1, PSIGN_MEGA, mreq->getPC(), 0);
        }
#else
        // Mega next level
        for (int i = 0; i < pendAddr_counter; i++) {
          router->tryPrefetch(pendAddr[i], mreq->has_stats(), 1, PSIGN_MEGA, mreq->getPC(), 0);
        }
        if (pendAddr[0] != page_addr) {
          router->tryPrefetch(page_addr, mreq->has_stats(), 1, PSIGN_MEGA, mreq->getPC(), 0);
        }
#endif
      }
    }
  }
#endif

  I(l->getSharingCount() == 0);

  return l;
}

void CCache::mustForwardReqDown(MemRequest *mreq, bool miss) {
  if (!mreq->isPrefetch()) {
    s_reqMissLine[mreq->getAction()]->inc(miss && mreq->has_stats());
    s_reqMissState[mreq->getAction()]->inc(!miss && mreq->has_stats());
  }

  if (mreq->getAction() == ma_setDirty) {
    mreq->adjustReqAction(ma_setExclusive);
  }

  I(!mreq->isRetrying());

  router->scheduleReq(mreq, 0);  // miss latency already charged
}

bool CCache::CState::shouldNotifyLowerLevels(MsgAction ma, bool inc) const {
  if (inc) {
    if (state == I) {
      return true;
    }
    return false;
  }

  switch (ma) {
    case ma_setValid: return state == I;
    case ma_setInvalid: return true;
    case ma_MMU:  // ARM does not cache MMU translations???
    case ma_setDirty: return state != M && state != E;
    case ma_setShared: return state != S && state != E;
    case ma_setExclusive: return state != M && state != E;
    default: I(0);
  }
  I(0);
  return false;
}

bool CCache::CState::shouldNotifyHigherLevels(MemRequest *mreq, int16_t portid) const {
#if 0
  if uncoherent, return false
#endif
  if (nSharers == 0) {
    return false;
  }

  if (nSharers == 1 && share[0] == portid) {
    return false;  // Nobody but requester
  }

#if 0
  if (incoherent){
    return false;
  }
#endif

  switch (mreq->getAction()) {
    case ma_setValid:
    case ma_setShared: return shareState != S;
    case ma_setDirty:
    case ma_setExclusive:
    case ma_setInvalid: return true;
    default: I(0);
  }
  I(0);
  return true;
}

CCache::CState::StateType CCache::CState::calcAdjustState(MemRequest *mreq) const {
  StateType nstate = state;
  switch (mreq->getAction()) {
    case ma_setValid:
      if (state == I) {
        nstate = E;  // Simpler not E state
      }
      // else keep the same
      break;
    case ma_setInvalid: nstate = I; break;
    case ma_setDirty: nstate = M; break;
    case ma_setShared: nstate = S; break;
    case ma_setExclusive:
      // I(state == I || state == E);
      nstate = E;
      break;
    default: I(0);
  }

  return nstate;
}

void CCache::CState::adjustState(MemRequest *mreq, int16_t portid) {
  StateType ostate = state;
  state            = calcAdjustState(mreq);

  // I(ostate != state); // only if we have full MSHR
  GI(mreq->isReq(), mreq->getAction() == ma_setExclusive || mreq->getAction() == ma_setDirty || mreq->getAction() == ma_setValid);
  GI(mreq->isReqAck(),
     mreq->getAction() == ma_setExclusive || mreq->getAction() == ma_setDirty || mreq->getAction() == ma_setShared);
  GI(mreq->isDisp(), mreq->getAction() == ma_setDirty || mreq->getAction() == ma_setValid);
  GI(mreq->isSetStateAck(), mreq->getAction() == ma_setShared || mreq->getAction() == ma_setInvalid);
  GI(mreq->isSetState(), mreq->getAction() == ma_setShared || mreq->getAction() == ma_setInvalid);

  if (mreq->isDisp()) {
    I(state != I);
    removeSharing(portid);
    if (mreq->getAction() == ma_setDirty) {
      state = M;
    } else {
      I(mreq->getAction() == ma_setValid);
      if (nSharers == 0) {
        state = I;
      }
    }
  } else if (mreq->isSetStateAck()) {
    if (mreq->getAction() == ma_setInvalid) {
      if (isBroadcastNeeded()) {
        nSharers = CCACHE_MAXNSHARERS - 1;  // Broadcast was sent, remove broadcast need
      }
      removeSharing(portid);
    } else {
      I(mreq->getAction() == ma_setShared);
      if (shareState != I) {
        shareState = S;
      }
    }
    if (mreq->isHomeNode()) {
      state = ostate;
      if (mreq->isSetStateAckDisp()) {
        // Only if it is the one that triggered the setState
        state = M;
      }
    }
  } else if (mreq->isSetState()) {
    //    if (nSharers)
    //      shareState = state;
    // I(portid<0);
  } else {
    I(state != I);
    I(mreq->isReq() || mreq->isReqAck());
    if (ostate != I && !mreq->isTopCoherentNode()) {
      // I(!mreq->isHomeNode());
      state = ostate;
    }
    if (mreq->isReqAck() && !mreq->isTopCoherentNode()) {
      switch (mreq->getAction()) {
        case ma_setDirty:
          if (ostate == I) {
            state = E;
          } else {
            state = ostate;
          }
          break;
        case ma_setShared: state = S; break;
        case ma_setExclusive: state = E; break;
        default: I(0);
      }
    }
    addSharing(portid);
    if (nSharers == 0) {
      shareState = I;
    } else if (nSharers > 1) {
      shareState = S;
    } else {
      if (state == S) {
        shareState = S;
      } else {
        I(state != I);
        shareState = E;
      }
    }
  }

  GI(nSharers > 1, shareState != E && shareState != M);
  GI(shareState == E || shareState == M, nSharers == 1);
  GI(nSharers == 0, shareState == I);
  GI(shareState == I, nSharers == 0);

  if (state == I && shareState == I) {
    invalidate();
    return;
  }
}

bool CCache::notifyLowerLevels(Line *l, MemRequest *mreq) {
  if (justDirectory) {
    return false;
  }

  if (!needsCoherence) {
    return false;
  }

  if (mreq->isReqAck()) {
    return false;
  }

  I(mreq->isReq());

  if (l->shouldNotifyLowerLevels(mreq->getAction(), incoherent)) {
    return true;
  }

  return false;
}

bool CCache::notifyHigherLevels(Line *l, MemRequest *mreq) {
  // Must do the notifyLowerLevels first
  I(l);
  I(!notifyLowerLevels(l, mreq));

  if (mreq->isTopCoherentNode()) {
    return false;
  }

  if (victim) {
    return false;
  }

  I(!mreq->isHomeNode());
  I(!router->isTopLevel());

  int16_t portid = router->getCreatorPort(mreq);
  if (l->shouldNotifyHigherLevels(mreq, portid)) {
    if (mreq->isPrefetch()) {
      dropPrefetch(mreq);
      return false;
    }
    MsgAction ma = l->othersNeed(mreq->getOrigAction());

    if (ma == ma_setShared && mreq->isReqAck()) {
      I(mreq->getAction() == ma_setShared || mreq->getAction() == ma_setExclusive);
      if (mreq->getAction() == ma_setExclusive) {
        mreq->forceReqAction(ma_setShared);
      }
    }

    trackAddress(mreq);
    // TODO: check that it is the correct DL1 IL1 request source

    if (!directory || l->isBroadcastNeeded()) {
      router->sendSetStateOthers(mreq, ma, inOrderUpMessage());
      // I(num); // Otherwise, the need coherent would be set
    } else {
      for (int16_t i = 0; i < l->getSharingCount(); i++) {
        int16_t pos = l->getSharingPos(i);
        if (pos != portid) {
          auto j = router->sendSetStateOthersPos(l->getSharingPos(i), mreq, ma, inOrderUpMessage());
          I(j);
        }
      }
    }

    // If mreq has pending stateack, it should not complete the read now
    return (mreq->hasPendingSetStateAck());
  }

  return false;
}

void CCache::CState::addSharing(int16_t id) {
  if (nSharers >= CCACHE_MAXNSHARERS) {
    I(shareState == S);
    return;
  }
  if (id < 0) {
    if (nSharers == 0) {
      shareState = I;
    }
    return;
  }

  I(id >= 0);  // portid<0 means no portid found
  if (nSharers == 0) {
    share[0] = id;
    nSharers++;
    GI(nSharers > 1, shareState == S);
    return;
  }

  for (int i = 0; i < nSharers; i++) {
    if (share[i] == id) {
      return;
    }
  }

  share[nSharers] = id;
  nSharers++;
}

void CCache::CState::removeSharing(int16_t id) {
  if (nSharers >= CCACHE_MAXNSHARERS) {
    return;  // not possible to remove if in broadcast mode
  }

  for (int16_t i = 0; i < nSharers; i++) {
    if (share[i] == id) {
      for (int16_t j = i; j < (nSharers - 1); j++) {
        share[j] = share[j + 1];
      }
      nSharers--;
      if (nSharers == 0) {
        shareState = I;
      }
      return;
    }
  }
}

void CCache::CState::set(const MemRequest *mreq) {
  if (mreq->isPrefetch()) {
    setPrefetch(mreq->getPC(), mreq->getSign(), mreq->getDegree());
  } else {
    clearPrefetch(mreq->getPC());
  }
}

void CCache::doReq(MemRequest *mreq) {
  MTRACE("doReq start ID:{} @{}", mreq->getID(), globalClock);

  trackAddress(mreq);

  Addr_t addr     = mreq->getAddr();
  bool   retrying = mreq->isRetrying();

  if (retrying) {  // reissued operation
    mreq->clearRetrying();
    // GI(mreq->isPrefetch() , !pmshr->canIssue(addr)); // the req is already queued if retrying
    GI(!mreq->isPrefetch(), !mshr->canIssue(addr));  // the req is already queued if retrying
    I(!mreq->isPrefetch());
  } else {
    if (!mshr->canIssue(addr)) {
      MTRACE("doReq queued");

      if (mreq->isPrefetch()) {
        dropPrefetch(mreq);
      } else {
        mreq->setRetrying();
        mshr->addEntry(addr, &mreq->redoReqCB, mreq);
      }
      return;
    }
    mshr->blockEntry(addr, mreq);
  }

  if (mreq->isNonCacheable()) {
    router->scheduleReq(mreq, 0);  // miss latency already charged
    return;
  }

  Line *l = 0;
  if (mreq->isPrefetch()) {
    l = cacheBank->findLineNoEffect(addr, mreq->getPC());
  } else {
    l = cacheBank->readLine(addr, mreq->getPC());
  }

  if (!allocateMiss && l == 0) {
    Addr_t page_addr = (addr >> 10) << 10;
    if (page_addr == addr && mreq->getSign() == PSIGN_MEGA) {
      // Time to allocate a megaLine
    } else {
      router->scheduleReq(mreq, 0);  // miss latency already charged
      return;
    }
  }

  if (nlp_enabled && !retrying && !mreq->isPrefetch()) {
    Addr_t base = addr + nlp_distance * lineSize;
    for (int i = 0; i < nlp_degree; i++) {
#if 1
      // Geometric stride
      // static int prog[] = {1,3,7,15,31,63,127,255,511};
      static int prog[] = {1, 3, 6, 25, 15, 76, 63, 229, 127, 458};
      int        delta;
      if (i < 8) {
        delta = prog[i];
      } else {
        delta = i * 67;
      }
      delta = delta * nlp_stride;
      tryPrefetch(base + (delta * lineSize), mreq->has_stats(), i, PSIGN_NLINE, mreq->getPC());
#else
      tryPrefetch(base + (i * nlp_stride * lineSize), mreq->has_stats(), i, PSIGN_NLINE, mreq->getPC());
#endif
    }
  }

  if (l && mreq->isPrefetch() && mreq->isHomeNode()) {
    nPrefetchDropped.inc(mreq->has_stats());
    mreq->setDropped();  // useless prefetch, already a hit
  }

  if (justDirectory && retrying && l == 0) {
    l = allocateLine(addr, mreq);
  }

  if (l == 0) {
    MTRACE("doReq cache miss");
    mustForwardReqDown(mreq, true);
    return;
  }

  if (l->isPrefetch() && !mreq->isPrefetch()) {
    nPrefetchUseful.inc(mreq->has_stats());
    I(!victim);  // Victim should not have prefetch lines
  }

  if (l->isPrefetch() && mreq->isPrefetch()) {
    nPrefetchRedundant.inc(mreq->has_stats());
  }

  l->clearPrefetch(mreq->getPC());

  if (notifyLowerLevels(l, mreq)) {
    MTRACE("doReq change state down");
    mustForwardReqDown(mreq, false);
    return;  // Done (no retrying), and wait for the ReqAck
  }

  I(!mreq->hasPendingSetStateAck());
  GI(mreq->isPrefetch(), !mreq->hasPendingSetStateAck());
  bool waitupper = notifyHigherLevels(l, mreq);
  if (waitupper) {
    MTRACE("doReq change state up others");
    I(mreq->hasPendingSetStateAck());
    // Not easy to drop prefetch
    mreq->setRetrying();
    return;
  }
  if (mreq->isDropped()) {
    // NotifyHigherLevel can trigger and setDropped
    port.reqRetire(mreq);
    mshr->retire(addr, mreq);
    return;
  }

  I(l);
  I(l->isValid() || justDirectory);  // JustDirectory can have newly allocated line with invalid state

  int16_t portid = router->getCreatorPort(mreq);
  GI(portid < 0, mreq->isTopCoherentNode());
  l->adjustState(mreq, portid);

  Time_t when = port.reqDone(mreq, retrying);
  if (when == 0) {
    // I(0);
    MTRACE("doReq restartReq");
    // Must restart request
    if (mreq->isPrefetch()) {
      dropPrefetch(mreq);
      port.reqRetire(mreq);
      mshr->retire(addr, mreq);
    } else {
      mreq->setRetrying();
      mreq->restartReq();
    }
    return;
  }

  //when = inOrderUpMessageAbs(when);

  if (justDirectory) {
    if (l->needsDisp()) {
      router->sendDirtyDisp(mreq->getAddr(), mreq->has_stats(), 1);
    }
    l->forceInvalid();
  }

  if (!mreq->isPrefetch()) {
    if (retrying) {
      s_reqHalfMiss[mreq->getAction()]->inc(mreq->has_stats());
    } else {
      s_reqHit[mreq->getAction()]->inc(mreq->has_stats());
    }
  }

  if (mreq->isDemandCritical()) {
    I(!mreq->isPrefetch());
#ifdef ENABLE_LDBP
    if (mreq->isTriggerLoad() && mreq->isHomeNode()) {
      // mreq->getHomeNode()->find_cir_queue_index(mreq);
      // mreq->getHomeNode()->lor_find_index(mreq);
      mreq->getHomeNode()->lor_find_index(mreq->getAddr());
    }
#endif
    double lat = mreq->getTimeDelay() + (when - globalClock);
    avgMemLat.sample(lat, mreq->has_stats());
    if (retrying) {
      avgHalfMemLat.sample(lat, mreq->has_stats());
    }

  } else if (mreq->isPrefetch()) {
    double lat = mreq->getTimeDelay() + (when - globalClock);
    avgPrefetchLat.sample(lat, mreq->has_stats());
  }

  I(when > globalClock);
  if (mreq->isHomeNode()) {
    mreq->ackAbs(when);
    auto *dinst = mreq->getDinst();
    if (dinst) {
      dinst->setFullMiss(false);
    }
  } else {
    mreq->convert2ReqAck(l->reqAckNeeds());
    router->scheduleReqAckAbs(mreq, when);
  }

  MTRACE("doReq done  ID:{} @{}", mreq->getID(), when);

  port.reqRetire(mreq);
  mshr->retire(addr, mreq);
}

void CCache::doDisp(MemRequest *mreq) {
  trackAddress(mreq);

  Addr_t addr = mreq->getAddr();

  // A disp being prefetch means that the line was prefetched but never used

  Line *l = cacheBank->findLineNoEffect(addr, mreq->getPC());
  if (l == 0 && victim && allocateMiss && !mreq->isPrefetch()) {
    MTRACE("doDisp allocateLine");
    l = allocateLine(addr, mreq);
  }
  if (l) {
    int16_t portid = router->getCreatorPort(mreq);
    l->adjustState(mreq, portid);
  }
  if (justDirectory) {  // Directory info kept, invalid line to trigger misses
    // router->sendDirtyDisp(addr, mreq->has_stats(), 1);
    if (l) {
      if (l->getSharingCount() == 0) {
        l->invalidate();
      }
    }
    router->scheduleDisp(mreq, 1);
    writeBack.inc(mreq->has_stats());
  } else if (!allocateMiss && l == 0) {
    router->scheduleDisp(mreq, 1);
    writeBack.inc(mreq->has_stats());
  } else {
    mreq->ack();
  }
}

void CCache::blockFill(MemRequest *mreq) { port.blockFill(mreq); }

void CCache::doReqAck(MemRequest *mreq) {
  MTRACE("doReqAck start");
  trackAddress(mreq);

  mreq->recoverReqAction();

  Time_t when = globalClock;
  if (!mreq->isDropped()) {
    if (!mreq->isNonCacheable()) {
      Addr_t addr = mreq->getAddr();
      Line  *l    = 0;
      if (mreq->isPrefetch()) {
        l = cacheBank->findLineNoEffect(addr, mreq->getPC());
      } else {
        l = cacheBank->readLine(addr, mreq->getPC());

#ifdef ENABLE_PTRCHASE
        if (mreq->getAddr() < 0x4000000000ULL) {  // filter stack addresses
          if (minMissAddr > mreq->getAddr()) {
            minMissAddr = mreq->getAddr();
          }
          if (maxMissAddr < mreq->getAddr()) {
            maxMissAddr = mreq->getAddr();
          }
        }

        Addr_t base = (addr >> 6) << 6;
        for (int i = 0; i < 16; i++) {
          Addr_t data = esesc_mem_read(base + i);
          if (data > minMissAddr && data < maxMissAddr) {
            tryPrefetch(data, mreq->has_stats(), 1, PSIGN_CHASE, mreq->getPC(), 0);
          }
        }
#endif
      }

      // It could be l!=0 if we requested a check in the lower levels to change state.
      if (l == 0) {
        if (!victim && allocateMiss) {
          MTRACE("doReqAck allocateLine");
          l = allocateLine(addr, mreq);
        } else if (!allocateMiss && mreq->isHomeNode()) {
          MTRACE("doReqAck allocateLine");
          l = allocateLine(addr, mreq);
        }
      } else {
        l->set(mreq);

        if (notifyHigherLevels(l, mreq)) {
          // FIXME I(0);
          MTRACE("doReqAck Notifying Higher Levels");
          I(mreq->hasPendingSetStateAck());
          return;
        }
      }

      // s_reqSetState[mreq->getAction()]->inc(mreq->has_stats());

      int16_t portid = router->getCreatorPort(mreq);
      GI(portid < 0, mreq->isTopCoherentNode());
      if (l) {
        l->adjustState(mreq, portid);

        if (justDirectory) {  // Directory info kept, invalid line to trigger misses
          l->forceInvalid();
        }
      } else {
        I(victim || !allocateMiss);  // Only victim can pass through
      }
    }
  }

  when = port.reqAckDone(mreq);
  if (when == 0) {
    MTRACE("doReqAck restartReqAck");
    // Must restart request
    if (mreq->isPrefetch()) {
      dropPrefetch(mreq);
      port.reqRetire(mreq);
      mshr->retire(mreq->getAddr(), mreq);
    } else {
      mreq->setRetrying();
      mreq->restartReqAckAbs(globalClock + 3);
    }
    return;
  }
  if (!mreq->isWarmup()) {
    when = inOrderUpMessageAbs(when);
  }

  port.reqRetire(mreq);

  if (mreq->isDemandCritical()) {
    I(!mreq->isPrefetch());
#ifdef ENABLE_LDBP
    if (mreq->isTriggerLoad() && mreq->isHomeNode()) {
      mreq->getHomeNode()->lor_find_index(mreq->getAddr());
    }
#endif
    double lat = mreq->getTimeDelay(when);
    avgMissLat.sample(lat, mreq->has_stats() && !mreq->isPrefetch());
    avgMemLat.sample(lat, mreq->has_stats() && !mreq->isPrefetch());
  } else if (mreq->isPrefetch()) {
    double lat = mreq->getTimeDelay(when);
    avgPrefetchLat.sample(lat, mreq->has_stats());
  }

  if (mreq->isHomeNode()) {
    MTRACE("doReqAck isHomeNode, calling ackAbs {}", when);
    auto *dinst = mreq->getDinst();
    if (dinst) {
      dinst->setFullMiss(true);
    }
    mreq->ackAbs(when);
  } else {
    MTRACE("doReqAck is Not HomeNode, calling ackAbsCB {}", when);
    router->scheduleReqAckAbs(mreq, when);
  }

  mshr->retire(mreq->getAddr(), mreq);
}

void CCache::doSetState(MemRequest *mreq) {
  trackAddress(mreq);
  I(!mreq->isHomeNode());

  GI(victim, needsCoherence);

  if (!inclusive || !needsCoherence) {
    // If not inclusive, do whatever we want
    I(mreq->getCurrMem() == this);
    mreq->convert2SetStateAck(ma_setInvalid, false);
    router->scheduleSetStateAck(mreq, 0);  // invalidate ack with nothing else if not inclusive

    MTRACE("scheduleSetStateAck without disp (incoherent cache)");
    return;
  }

  Line *l = cacheBank->findLineNoEffect(mreq->getAddr());
  if (victim) {
    I(needsCoherence);
    invAll.inc(mreq->has_stats());
    int32_t nmsg = router->sendSetStateAll(mreq, mreq->getAction(), inOrderUpMessage());
    if (l) {
      l->invalidate();
    }
    I(nmsg);
    return;
  }

  if (l == 0) {
    // Done!
    mreq->convert2SetStateAck(ma_setInvalid, false);
    router->scheduleSetStateAck(mreq, 0);

    MTRACE("scheduleSetStateAck without disp (local miss)");
    return;
  }

  // FIXME: add hit/mixx delay

  int16_t portid = router->getCreatorPort(mreq);
  if (l->getSharingCount()) {
    if (portid >= 0) {
      l->removeSharing(portid);
    }
    if (directory) {
      if (l->getSharingCount() == 1) {
        invOne.inc(mreq->has_stats());
        int32_t i = router->sendSetStateOthersPos(l->getFirstSharingPos(), mreq, mreq->getAction(), inOrderUpMessage());
        I(i);
      } else {
        invAll.inc(mreq->has_stats());
        // FIXME: optimize directory for 2 or more
        int32_t nmsg = router->sendSetStateAll(mreq, mreq->getAction(), inOrderUpMessage());
        I(nmsg);
      }
    } else {
      invAll.inc(mreq->has_stats());
      int32_t nmsg = router->sendSetStateAll(mreq, mreq->getAction(), inOrderUpMessage());
      I(nmsg);
    }
  } else {
    invNone.inc(mreq->has_stats());
    // We are done
    bool needsDisp = l->needsDisp();
    l->adjustState(mreq, portid);
    GI(mreq->getAction() == ma_setInvalid, !l->isValid());
    GI(mreq->getAction() == ma_setShared, l->isShared());

    mreq->convert2SetStateAck(mreq->getAction(), needsDisp);  // Keep shared or invalid
    router->scheduleSetStateAck(mreq, 0);

    MTRACE("scheduleSetStateAck {} disp", needsDisp ? "with" : "without");
  }
}

void CCache::doSetStateAck(MemRequest *mreq) {
  trackAddress(mreq);

  Line *l = cacheBank->findLineNoEffect(mreq->getAddr());
  if (l) {
    bool    needsDisp = l->needsDisp();
    int16_t portid    = router->getCreatorPort(mreq);

    l->adjustState(mreq, portid);
    if (needsDisp) {
      mreq->setNeedsDisp();
    }
  }

  if (mreq->isHomeNode()) {
    MTRACE("scheduleSetStateAck {} disp (home) line is {}", mreq->isDisp() ? "with" : "without", l ? l->getState() : -1);

    avgSnoopLat.sample(mreq->getTimeDelay() + 0, mreq->has_stats());
    mreq->ack();  // same cycle
  } else {
    router->scheduleSetStateAck(mreq, 1);
    MTRACE("scheduleSetStateAck {} disp (forward)", mreq->isDisp() ? "with" : "without");
  }
}

void CCache::req(MemRequest *mreq) {
  // predicated ARM instructions can be with zero address
  if (mreq->getAddr() == 0) {
    mreq->ack(1);
    return;
  }

  if (!incoherent) {
    mreq->trySetTopCoherentNode(this);
  }

  port.req(mreq);
}

void CCache::reqAck(MemRequest *mreq) {
  // I(!mreq->isRetrying());
  //
  MTRACE("Received reqAck request");
  if (mreq->isRetrying()) {
    MTRACE("reqAck clearRetrying");
    mreq->clearRetrying();
  } else {
    s_reqAck[mreq->getAction()]->inc(mreq->has_stats());
  }

  port.reqAck(mreq);
}

void CCache::setState(MemRequest *mreq) {
  if (incoherent) {
    mreq->convert2SetStateAck(ma_setInvalid, true);
  }

  s_reqSetState[mreq->getAction()]->inc(mreq->has_stats());

  I(!mreq->isRetrying());
  port.setState(mreq);
}

void CCache::setStateAck(MemRequest *mreq) {
  I(!mreq->isRetrying());
  port.setStateAck(mreq);
}

void CCache::disp(MemRequest *mreq) {
  displacedRecv.inc(mreq->has_stats());
  I(!mreq->isRetrying());
  port.disp(mreq);
}

void CCache::tryPrefetch(Addr_t paddr, bool doStats, int degree, Addr_t pref_sign, Addr_t pc, CallbackBase *cb) {
  if ((paddr >> 8) == 0) {
    if (cb) {
      cb->destroy();
    }
    return;
  }

  I(degree < 40);

  nTryPrefetch.inc(doStats);

  if (cacheBank->findLineNoEffect(paddr)) {
    nPrefetchHitLine.inc(doStats);
    if (cb) {
      // static_cast<IndirectAddressPredictor::performedCB *>(cb)->setParam1(this);
      cb->call();
    }
    return;
  }

  if (!mshr->canIssue(paddr)) {
    nPrefetchHitPending.inc(doStats);
    if (cb) {
      cb->destroy();
    }
    return;
  }

  if (!allocateMiss) {
    Addr_t page_addr = (paddr >> 10) << 10;
    if (pref_sign != PSIGN_MEGA || page_addr != paddr) {
      nPrefetchHitBusy.inc(doStats);
      router->tryPrefetch(paddr, doStats, degree, pref_sign, pc, cb);
      return;
    }
  }

  if (port.isBusy(paddr) || degree > prefetch_degree || victim) {
    nPrefetchHitBusy.inc(doStats);
    router->tryPrefetch(paddr, doStats, degree, pref_sign, pc, cb);
    return;
  }

  nSendPrefetch.inc(doStats);
  if (cb) {
    // I(pref_sign==PSIGN_STRIDE);
    // static_cast<IndirectAddressPredictor::performedCB *>(cb)->setParam1(this);
  }
  MemRequest *preq = MemRequest::createReqReadPrefetch(this, doStats, paddr, pref_sign, degree, pc, cb);
  preq->trySetTopCoherentNode(this);
  port.startPrefetch(preq);
  router->scheduleReq(preq, 1);
  mshr->blockEntry(paddr, preq);
}

bool CCache::isBusy(Addr_t addr) const {
  if (port.isBusy(addr)) {
    return true;
  }

  return false;
}

void CCache::dump() const { mshr->dump(); }

TimeDelta_t CCache::ffread(Addr_t addr) {
  Addr_t addr_r = 0;

  Line *l = cacheBank->readLine(addr);
  if (l) {
    return 1;  // done!
  }

  l = cacheBank->fillLine_replace(addr, addr_r, 0xbeefbeef);
  l->setExclusive();  // WARNING, can create random inconsistencies (no inv others)

  return router->ffread(addr) + 1;
}

TimeDelta_t CCache::ffwrite(Addr_t addr) {
  Addr_t addr_r = 0;

  Line *l = cacheBank->writeLine(addr);
  if (l == 0) {
    l = cacheBank->fillLine_replace(addr, addr_r, 0xbeefbeef);
  }
  if (router->isTopLevel()) {
    l->setModified();  // WARNING, can create random inconsistencies (no inv others)
  } else {
    l->setExclusive();
  }

  return router->ffwrite(addr) + 1;
}

void CCache::setNeedsCoherence() { needsCoherence = true; }

void CCache::clearNeedsCoherence() { needsCoherence = false; }
