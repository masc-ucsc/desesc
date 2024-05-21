// See license.txt for details.

#pragma once

#include <vector>

#include "cache_port.hpp"
#include "cachecore.hpp"
#include "estl.hpp"
#include "gprocessor.hpp"
#include "memobj.hpp"
#include "memory_system.hpp"
#include "mshr.hpp"
#include "sctable.hpp"
#include "snippets.hpp"
#include "stats.hpp"

// #include "Prefetcher.h"

class MemRequest;

#define CCACHE_MAXNSHARERS 64

// #define ENABLE_PTRCHASE 1

class CCache : public MemObj {
protected:
  class CState : public StateGeneric<Addr_t> { /*{{{*/
  private:
    enum StateType { M, E, S, I };
    StateType state;
    StateType shareState;

    int16_t nSharers;
    int16_t share[CCACHE_MAXNSHARERS];  // Max number of shares to remember. If nshares >=8, then broadcast

  public:
    CState(int32_t lineSize) {
      (void)lineSize;
      state = I;
      clearTag();
    }

    bool isModified() const { return state == M; }
    void setModified() { state = M; }
    bool isExclusive() const { return state == E; }
    void setExclusive() { state = E; }
    bool isShared() const { return state == S; }
    void setShared() { state = S; }
    bool isValid() const { return state != I || shareState != I; }
    bool isLocalInvalid() const { return state == I; }

    void forceInvalid() { state = I; }

    // If SNOOPS displaces E too
    // bool needsDisp() const { return state == M || state == E; }
    bool needsDisp() const { return state == M; }

    bool      shouldNotifyLowerLevels(MsgAction ma, bool incoherent) const;
    bool      shouldNotifyHigherLevels(MemRequest *mreq, int16_t port_id) const;
    StateType getState() const { return state; };
    StateType calcAdjustState(MemRequest *mreq) const;
    void      adjustState(MemRequest *mreq, int16_t port_id);

    static MsgAction othersNeed(MsgAction ma) {
      switch (ma) {
        case ma_setValid: return ma_setShared;
        case ma_setDirty: return ma_setInvalid;
        default: I(0);
      }
      I(0);
      return ma_setDirty;
    }
    MsgAction reqAckNeeds() const {
      switch (shareState) {
        case M: return ma_setDirty;
        case E: return ma_setExclusive;
        case S: return ma_setShared;
        case I: return ma_setInvalid;
      }
      I(0);
      return ma_setDirty;
    }

    // bool canRead()  const { return state != I; }
    // bool canWrite() const { return state == E || state == M; }

    void invalidate() {
      state      = I;
      nSharers   = 0;
      shareState = I;
      clearTag();
    }

    bool isBroadcastNeeded() const { return nSharers >= CCACHE_MAXNSHARERS; }

    int16_t getSharingCount() const {
      return nSharers;  // Directory
    }
    void    removeSharing(int16_t id);
    void    addSharing(int16_t id);
    int16_t getFirstSharingPos() const { return share[0]; }
    int16_t getSharingPos(int16_t pos) const {
      I(pos < nSharers);
      return share[pos];
    }
    void clearSharing() { nSharers = 0; }

    void set(const MemRequest *mreq);
  }; /*}}}*/

  typedef CacheGeneric<CState, Addr_t>            CacheType;
  typedef CacheGeneric<CState, Addr_t>::CacheLine Line;

  CacheType *cacheBank;
  MSHR      *mshr;
  MSHR      *pmshr;

  Time_t lastUpMsg;  // can not bypass up messages (races)
  Time_t inOrderUpMessageAbs(Time_t when) {
#if 1
    if (lastUpMsg > when) {
      when = lastUpMsg;
    } else {
      lastUpMsg = when;
    }
#endif
    I(when >= globalClock);
    if (when == globalClock) {
      when++;
    }

    return when;
  }
  TimeDelta_t inOrderUpMessage(TimeDelta_t lat = 0) {
#if 1
    if (lastUpMsg > globalClock) {
      lat += (lastUpMsg - globalClock);
    }

    lastUpMsg = globalClock + lat;
#endif

    return lat;
  }

  int32_t lineSize;
  int32_t lineSizeBits;

  bool    nlp_enabled;
  int32_t nlp_degree;
  int32_t nlp_distance;
  int32_t nlp_stride;

  int32_t prefetch_degree;
  double  prefetch_megaratio;

  int32_t moving_conf;

  bool coreCoupledFreq;
  bool inclusive;
  bool directory;
  bool needsCoherence;
  bool incoherent;
  bool victim;
  bool allocateMiss;
  bool justDirectory;

  // BEGIN Statistics
  Stats_cntr nTryPrefetch;
  Stats_cntr nSendPrefetch;

  Stats_cntr displacedSend;
  Stats_cntr displacedRecv;

  Stats_cntr invAll;
  Stats_cntr invOne;
  Stats_cntr invNone;

  Stats_cntr writeBack;

  Stats_cntr lineFill;

  Stats_avg avgMissLat;
  Stats_avg avgMemLat;
  Stats_avg avgHalfMemLat;
  Stats_avg avgSnoopLat;
  Stats_avg avgPrefetchLat;

  Stats_cntr capInvalidateHit;
  Stats_cntr capInvalidateMiss;
  Stats_cntr invalidateHit;
  Stats_cntr invalidateMiss;
  Stats_cntr writeExclusive;

  Stats_cntr nPrefetchUseful;
  Stats_cntr nPrefetchWasteful;
  Stats_cntr nPrefetchLineFill;
  Stats_cntr nPrefetchRedundant;
  Stats_cntr nPrefetchHitLine;
  Stats_cntr nPrefetchHitPending;
  Stats_cntr nPrefetchHitBusy;
  Stats_cntr nPrefetchDropped;

  Stats_cntr *s_reqHit[ma_MAX];
  Stats_cntr *s_reqMissLine[ma_MAX];
  Stats_cntr *s_reqMissState[ma_MAX];
  Stats_cntr *s_reqHalfMiss[ma_MAX];
  Stats_cntr *s_reqAck[ma_MAX];
  Stats_cntr *s_reqSetState[ma_MAX];

  // Statistics currently not used.
  // Only defined here to prevent bogus warnings from the powermodel.

  Addr_t minMissAddr;
  Addr_t maxMissAddr;

  // END Statistics
  void  displaceLine(Addr_t addr, MemRequest *mreq, Line *l);
  Line *allocateLine(Addr_t addr, MemRequest *mreq);
  void  mustForwardReqDown(MemRequest *mreq, bool miss);

  bool notifyLowerLevels(Line *l, MemRequest *mreq);
  bool notifyHigherLevels(Line *l, MemRequest *mreq);

  void dropPrefetch(MemRequest *mreq);

  void
  cleanup();  // FIXME: Expose this to MemObj and call it from core on ctx switch or syscall (move to public and remove callback)
  StaticCallbackMember0<CCache, &CCache::cleanup> cleanupCB;
  Cache_port                                      port;

public:
  CCache(Memory_system *gms, const std::string &descr_section, const std::string &name);
  virtual ~CCache();

  int32_t getLineSize() const { return lineSize; }

  // Entry points to schedule that may schedule a do?? if needed
  void req(MemRequest *req);
  void blockFill(MemRequest *req);
  void reqAck(MemRequest *req);
  void setState(MemRequest *req);
  void setStateAck(MemRequest *req);
  void disp(MemRequest *req);

  void tryPrefetch(Addr_t paddr, bool doStats, int degree, Addr_t pref_sign, Addr_t pc, CallbackBase *cb = 0);

  // This do the real work
  void doReq(MemRequest *req);
  void doReqAck(MemRequest *req);
  void doSetState(MemRequest *req);
  void doSetStateAck(MemRequest *req);
  void doDisp(MemRequest *req);

  TimeDelta_t ffread(Addr_t addr);
  TimeDelta_t ffwrite(Addr_t addr);

  bool isBusy(Addr_t addr) const;

  void dump() const;

  void setNeedsCoherence();
  void clearNeedsCoherence();

  bool isJustDirectory() const { return justDirectory; }

  bool Modified(Addr_t addr) const {
    Line *cl = cacheBank->findLineNoEffect(addr);
    if (cl != 0) {
      return cl->isModified();
    }

    return false;
  }

  bool Exclusive(Addr_t addr) const {
    Line *cl = cacheBank->findLineNoEffect(addr);
    if (cl != 0) {
      return cl->isExclusive();
    }

    return false;
  }

  bool Shared(Addr_t addr) const {
    Line *cl = cacheBank->findLineNoEffect(addr);
    if (cl != 0) {
      return cl->isShared();
    }
    return false;
  }

  bool Invalid(Addr_t addr) const {
    Line *cl = cacheBank->findLineNoEffect(addr);
    if (cl == 0) {
      return true;
    }
    return cl->isLocalInvalid();
  }

#ifndef NDEBUG
  void trackAddress(MemRequest *mreq);
#else
  void trackAddress(MemRequest *mreq) { (void)mreq; }
#endif
};
