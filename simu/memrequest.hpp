// See LICENSE for details.

#pragma once

#include "iassert.hpp"
#include "memobj.hpp"
#include "mrouter.hpp"

#ifndef NDEBUG
// #define DEBUG_CALLPATH 1
#endif

class MemRequest {
private:
  void setNextHop(MemObj *m);
  void startReq();
  void startReqAck();
  void startSetState();
  void startSetStateAck();
  void startDisp();

  uint64_t id;

  // memRequest pool {{{1
  static pool<MemRequest> actPool;
  friend class pool<MemRequest>;
  // }}}
  /* MsgType declarations {{{1 */
  enum MsgType { mt_req, mt_reqAck, mt_setState, mt_setStateAck, mt_disp };

#ifdef DEBUG_CALLPATH
  class CallEdge {
  public:
    const MemObj *s;      // start
    const MemObj *e;      // end
    Time_t        tismo;  // Time In Start Memory Object
    MsgType       mt;
    MsgAction     ma;
  };
#endif
  /* }}} */

  /* Local variables {{{1 */
  Addr_t    addr;
  MsgType   mt;
  MsgAction ma;
  MsgAction ma_orig;

  MemObj   *creatorObj;
  MemObj   *homeMemObj;       // Starting home node
  MemObj   *topCoherentNode;  // top cache
  MemObj   *currMemObj;
  MemObj   *prevMemObj;
  MemObj   *firstCache;
  MsgAction firstCache_ma;

#ifdef DEBUG_CALLPATH
  std::vector<CallEdge> calledge;
  Time_t                lastCallTime;
#endif

  CallbackBase *cb;

  int16_t     pendingSetStateAck;
  MemRequest *setStateAckOrig;

  Time_t startClock;

  bool prefetch;  // This means that can be dropped at will
  bool spec;
  bool notifyScbDirectly;
  bool dropped;
  bool retrying;
  bool needsDisp;  // Once set, it keeps the value
  bool keep_stats;
  bool warmup;
  bool nonCacheable;

  // trigger load params
  bool    trigger_load;  // flag to trigger load
  bool    ld_used;       //
  Addr_t  base_addr;
  Addr_t  end_addr;
  Addr_t  dep_pc;
  int64_t delta;
  int64_t delta2;
  int64_t inflight;
  int     dep_pc_count;
  int     ld_br_type;
  int     dep_depth;
  int     tl_type;

  Addr_t  pc;
  Dinst  *dinst;      // WARNING: valid IFF demand DL1
  Addr_t  pref_sign;  // WARNING: valid IFF prefetch is true
  int32_t degree;     // WARNING: valid IFF prefetch is true
  /* }}} */

  MemRequest();
  virtual ~MemRequest();

  void memReq();     // E.g: L1 -> L2
  void memReqAck();  // E.gL L2 -> L1 ack

  void memSetState();     // E.g: L2 -> L1
  void memSetStateAck();  // E.gL L1 -> L2 ack

  void memDisp();  // E.g: L1 -> L2

  friend class MRouter;  // only mrouter can call the req directly
  void redoReq(TimeDelta_t lat) { redoReqCB.schedule(lat); }
  void redoReqAck(TimeDelta_t lat) { redoReqAckCB.schedule(lat); }
  void redoSetState(TimeDelta_t lat) { redoSetStateCB.schedule(lat); }
  void redoSetStateAck(TimeDelta_t lat) { redoSetStateAckCB.schedule(lat); }
  void redoDisp(TimeDelta_t lat) { redoDispCB.schedule(lat); }

  void startReq(MemObj *m, TimeDelta_t lat) {
    setNextHop(m);
    startReqCB.schedule(lat);
  }
  void startReqAck(MemObj *m, TimeDelta_t lat) {
    setNextHop(m);
    startReqAckCB.schedule(lat);
  }
  void startSetState(MemObj *m, TimeDelta_t lat) {
    setNextHop(m);
    startSetStateCB.schedule(lat);
  }
  void startSetStateAck(MemObj *m, TimeDelta_t lat) {
    setNextHop(m);
    startSetStateAckCB.schedule(lat);
  }
  void startDisp(MemObj *m, TimeDelta_t lat) {
    setNextHop(m);
    startDispCB.schedule(lat);
  }

  void setStateAckDone(TimeDelta_t lat);

#ifdef DEBUG_CALLPATH
public:
  static void dump_all();
  void        rawdump_calledge(TimeDelta_t lat = 0, Time_t total = 0);

protected:
  void dump_calledge(TimeDelta_t lat, bool interesting = false);
  void upce();
#else
  void rawdump_calledge(TimeDelta_t lat = 0, Time_t total = 0) {
    (void)lat;
    (void)total;
  };
  void dump_calledge(TimeDelta_t lat) { (void)lat; }
  void upce() {};
#endif
  static MemRequest *create(MemObj *m, Addr_t addr, bool keep_stats, CallbackBase *cb);

public:
  void redoReq();
  void redoReqAck();
  void redoSetState();
  void redoSetStateAck();
  void redoDisp();

  StaticCallbackMember0<MemRequest, &MemRequest::redoReq>         redoReqCB;
  StaticCallbackMember0<MemRequest, &MemRequest::redoReqAck>      redoReqAckCB;
  StaticCallbackMember0<MemRequest, &MemRequest::redoSetState>    redoSetStateCB;
  StaticCallbackMember0<MemRequest, &MemRequest::redoSetStateAck> redoSetStateAckCB;
  StaticCallbackMember0<MemRequest, &MemRequest::redoDisp>        redoDispCB;

  StaticCallbackMember0<MemRequest, &MemRequest::startReq>         startReqCB;
  StaticCallbackMember0<MemRequest, &MemRequest::startReqAck>      startReqAckCB;
  StaticCallbackMember0<MemRequest, &MemRequest::startSetState>    startSetStateCB;
  StaticCallbackMember0<MemRequest, &MemRequest::startSetStateAck> startSetStateAckCB;
  StaticCallbackMember0<MemRequest, &MemRequest::startDisp>        startDispCB;

  void redoReqAbs(Time_t when) { redoReqCB.scheduleAbs(when); }
  void startReqAbs(MemObj *m, Time_t when) {
    setNextHop(m);
    startReqCB.scheduleAbs(when);
  }
  void restartReq() { startReq(); }

  void redoReqAckAbs(Time_t when) { redoReqAckCB.scheduleAbs(when); }
  void startReqAckAbs(MemObj *m, Time_t when) {
    setNextHop(m);
    startReqAckCB.scheduleAbs(when);
  }
  void restartReqAck() { startReqAck(); }
  void restartReqAckAbs(Time_t when) { startReqAckCB.scheduleAbs(when); }

  void redoSetStateAbs(Time_t when) { redoSetStateCB.scheduleAbs(when); }
  void startSetStateAbs(MemObj *m, Time_t when) {
    setNextHop(m);
    startSetStateCB.scheduleAbs(when);
  }

  void redoSetStateAckAbs(Time_t when) { redoSetStateAckCB.scheduleAbs(when); }
  void startSetStateAckAbs(MemObj *m, Time_t when) {
    setNextHop(m);
    startSetStateAckCB.scheduleAbs(when);
  }

  void redoDispAbs(Time_t when) { redoDispCB.scheduleAbs(when); }
  void startDispAbs(MemObj *m, Time_t when) {
    setNextHop(m);
    startDispCB.scheduleAbs(when);
  }

  static void sendReqVPCWriteUpdate(MemObj *m, bool keep_stats, Addr_t addr) {
    MemRequest *mreq = create(m, addr, keep_stats, nullptr);
    mreq->mt         = mt_req;
    mreq->ma         = ma_VPCWU;
    mreq->ma_orig    = mreq->ma;
    m->req(mreq);
  }
  static MemRequest *createReqReadPrefetch(MemObj *m, bool keep_stats, Addr_t addr, Addr_t pref_sign, int32_t degree, Addr_t pc,
                                           CallbackBase *cb = 0) {
    MemRequest *mreq      = create(m, addr, keep_stats, cb);
    mreq->prefetch        = true;
    mreq->topCoherentNode = 0;
    mreq->mt              = mt_req;
    mreq->ma              = ma_setValid;  // For reads, MOES are valid states
    mreq->ma_orig         = mreq->ma;
    mreq->pc              = pc;
    mreq->degree          = degree;
    mreq->pref_sign       = pref_sign;
    return mreq;
  }
  [[nodiscard]] int32_t getDegree() const { return degree; }
  [[nodiscard]] Addr_t  getSign() const { return pref_sign; }

  static void sendNCReqRead(MemObj *m, bool keep_stats, Addr_t addr, CallbackBase *cb = nullptr) {
    MemRequest *mreq   = create(m, addr, keep_stats, cb);
    mreq->mt           = mt_req;
    mreq->ma           = ma_setValid;  // For reads, MOES are valid states
    mreq->ma_orig      = mreq->ma;
    mreq->nonCacheable = true;
    m->req(mreq);
  }

  static MemRequest *createSpecReqRead(MemObj *m, bool keep_stats, Addr_t addr, Addr_t pc, CallbackBase *cb = nullptr) {
    MemRequest *mreq        = create(m, addr, keep_stats, cb);
    mreq->spec              = true;
    mreq->notifyScbDirectly = false;
    mreq->mt                = mt_req;
    mreq->ma                = ma_setValid;  // For reads, MOES are valid states
    mreq->ma_orig           = mreq->ma;
    mreq->pc                = pc;
    return mreq;
  }

  static MemRequest *createSafeReqRead(MemObj *m, bool keep_stats, Addr_t addr, Addr_t pc, CallbackBase *cb = nullptr) {
    MemRequest *mreq = create(m, addr, keep_stats, cb);
    I(!mreq->spec);
    mreq->notifyScbDirectly = false;
    mreq->mt                = mt_req;
    mreq->ma                = ma_setValid;  // For reads, MOES are valid states
    mreq->ma_orig           = mreq->ma;
    mreq->pc                = pc;
    return mreq;
  }

  static MemRequest *createReqRead(MemObj *m, bool keep_stats, Addr_t addr, Addr_t pc, CallbackBase *cb = nullptr) {
    MemRequest *mreq = create(m, addr, keep_stats, cb);
    mreq->mt         = mt_req;
    mreq->ma         = ma_setValid;  // For reads, MOES are valid states
    mreq->ma_orig    = mreq->ma;
    mreq->pc         = pc;
    return mreq;
  }

  static void triggerReqRead(MemObj *m, bool keep_stats, Addr_t trig_addr, Addr_t pc, Addr_t _dep_pc, Addr_t _start_addr,
                             Addr_t _end_addr, int64_t _delta, int64_t _inf, int _ld_br_type, int _depth, int tl_type,
                             bool _ld_used, CallbackBase *cb = nullptr) {
    MemRequest *mreq   = createReqRead(m, keep_stats, trig_addr, pc, cb);
    mreq->trigger_load = true;
    mreq->dep_pc       = _dep_pc;
    mreq->base_addr    = _start_addr;
    mreq->end_addr     = _end_addr;
    mreq->delta        = _delta;
    mreq->inflight     = _inf;
    mreq->ld_br_type   = _ld_br_type;
    mreq->dep_depth    = _depth;
    mreq->tl_type      = tl_type;
    mreq->ld_used      = _ld_used;
    m->req(mreq);
  }

  static void sendReqRead(MemObj *m, bool keep_stats, Addr_t addr, Addr_t pc, CallbackBase *cb = nullptr) {
    MemRequest *mreq = createReqRead(m, keep_stats, addr, pc, cb);
    m->req(mreq);
  }
  static void sendSpecReqDL1Read(MemObj *m, bool keep_stats, Addr_t addr, Addr_t pc, Dinst *dinst, CallbackBase *cb) {
    MemRequest *mreq = createSpecReqRead(m, keep_stats, addr, pc, cb);
    mreq->dinst      = dinst;
    m->req(mreq);
  }

  static void sendSafeReqDL1Read(MemObj *m, bool keep_stats, Addr_t addr, Addr_t pc, Dinst *dinst, CallbackBase *cb) {
    MemRequest *mreq = createSafeReqRead(m, keep_stats, addr, pc, cb);
    mreq->dinst      = dinst;
    m->req(mreq);
  }

  static void sendReqDL1Read(MemObj *m, bool keep_stats, Addr_t addr, Addr_t pc, Dinst *dinst, CallbackBase *cb) {
    MemRequest *mreq = createReqRead(m, keep_stats, addr, pc, cb);
    mreq->dinst      = dinst;
    m->req(mreq);
  }

  static void sendReqReadWarmup(MemObj *m, Addr_t addr) {
    MemRequest *mreq = create(m, addr, false, nullptr);
    mreq->mt         = mt_req;
    mreq->ma         = ma_setValid;  // For reads, MOES are valid states
    mreq->ma_orig    = mreq->ma;
    mreq->warmup     = true;
    m->req(mreq);
  }

  static void sendReqWriteWarmup(MemObj *m, Addr_t addr) {
    MemRequest *mreq = create(m, addr, false, nullptr);
    mreq->mt         = mt_req;
    mreq->ma         = ma_setDirty;  // For writes, only MO are valid states
    mreq->ma_orig    = mreq->ma;
    mreq->warmup     = true;
    m->req(mreq);
  }
  static void sendNCReqWrite(MemObj *m, bool keep_stats, Addr_t addr, CallbackBase *cb = nullptr) {
    MemRequest *mreq   = create(m, addr, keep_stats, cb);
    mreq->mt           = mt_req;
    mreq->ma           = ma_setDirty;  // For writes, only MO are valid states
    mreq->ma_orig      = mreq->ma;
    mreq->nonCacheable = true;
    m->req(mreq);
  }

  static void sendReqWrite(MemObj *m, bool keep_stats, Addr_t addr, Addr_t pc, CallbackBase *cb = nullptr) {
    MemRequest *mreq = create(m, addr, keep_stats, cb);
    mreq->mt         = mt_req;
    mreq->ma         = ma_setDirty;  // For writes, only MO are valid states
    mreq->ma_orig    = mreq->ma;
    mreq->pc         = pc;
    m->req(mreq);
  }
  static void sendReqWritePrefetch(MemObj *m, bool keep_stats, Addr_t addr, CallbackBase *cb = nullptr) {
    MemRequest *mreq = create(m, addr, keep_stats, cb);
    mreq->mt         = mt_req;
    mreq->ma         = ma_setDirty;
    mreq->ma_orig    = mreq->ma;
    m->req(mreq);
  }

  [[nodiscard]] bool isTriggerLoad() const { return trigger_load; }

  [[nodiscard]] bool is_spec() const { return spec; }
  [[nodiscard]] bool is_safe() const { return !spec; }

  [[nodiscard]] bool isLoadUsed() const { return ld_used; }

  [[nodiscard]] int getTLType() const { return tl_type; }

  [[nodiscard]] int getDepDepth() const { return dep_depth; }

  [[nodiscard]] int getLBType() const { return ld_br_type; }

  [[nodiscard]] Addr_t getBaseAddr() const { return base_addr; }

  [[nodiscard]] Addr_t getEndAddr() const { return end_addr; }

  [[nodiscard]] Addr_t getDepPC() const { return dep_pc; }

  [[nodiscard]] int64_t getDelta() const { return delta; }

  [[nodiscard]] int64_t getInflight() const { return inflight; }

  [[nodiscard]] int getDepCount() const { return dep_pc_count; }

  [[nodiscard]] bool isDemandCritical() const { return (mt == mt_req || mt == mt_reqAck) && !prefetch; }
  void               markNonCacheable() { nonCacheable = true; }
  [[nodiscard]] bool isNonCacheable() const { return nonCacheable; }

  void forceReqAction(MsgAction _ma) { ma = _ma; }

  void adjustReqAction(MsgAction _ma) {
    if (firstCache) {
      return;
    }

    firstCache    = currMemObj;
    firstCache_ma = ma;
    I(mt == mt_req);
    I(_ma == ma_setExclusive);
    ma = _ma;
  }
  void recoverReqAction() {
    I(mt == mt_reqAck || prefetch);
    if (firstCache != currMemObj) {
      return;
    }
    firstCache = nullptr;
    ma         = firstCache_ma;
  }

  void convert2ReqAck(MsgAction _ma) {
    I(mt == mt_req);
    ma = _ma;
    mt = mt_reqAck;
  }
  void convert2SetStateAck(MsgAction _ma, bool _needsDisp) {
    I(mt == mt_setState);
    mt         = mt_setStateAck;
    ma         = _ma;
    creatorObj = currMemObj;
    needsDisp  = _needsDisp;
  }

  static void sendDirtyDisp(MemObj *m, MemObj *creator, Addr_t addr, bool keep_stats, CallbackBase *cb = nullptr) {
    MemRequest *mreq = create(m, addr, keep_stats, cb);
    mreq->mt         = mt_disp;
    mreq->ma         = ma_setDirty;
    mreq->ma_orig    = mreq->ma;
    I(creator);
    mreq->creatorObj      = creator;
    mreq->topCoherentNode = creator;
    m->disp(mreq);
  }
  static void sendCleanDisp(MemObj *m, MemObj *creator, Addr_t addr, bool prefetch, bool keep_stats) {
    MemRequest *mreq = create(m, addr, keep_stats, nullptr);
    mreq->mt         = mt_disp;
    mreq->ma         = ma_setValid;
    mreq->ma_orig    = mreq->ma;
    mreq->prefetch   = prefetch;
    I(creator);
    mreq->creatorObj      = creator;
    mreq->topCoherentNode = creator;
    m->disp(mreq);
  }

  static MemRequest *createSetState(MemObj *m, MemObj *creator, MsgAction ma, Addr_t naddr, bool keep_stats) {
    MemRequest *mreq = create(m, naddr, keep_stats, nullptr);
    mreq->mt         = mt_setState;
    mreq->ma         = ma;
    mreq->ma_orig    = mreq->ma;
    I(creator);
    mreq->creatorObj = creator;
    return mreq;
  }

  [[nodiscard]] bool isReq() const { return mt == mt_req; }
  [[nodiscard]] bool isReqAck() const { return mt == mt_reqAck; }
  [[nodiscard]] bool isSetState() const { return mt == mt_setState; }
  [[nodiscard]] bool isSetStateAck() const { return mt == mt_setStateAck; }
  [[nodiscard]] bool isSetStateAckDisp() const { return mt == mt_setStateAck && needsDisp; }
  [[nodiscard]] bool isDisp() const { return mt == mt_disp; }

  void setDropped() {
    I(prefetch);
    dropped = true;
  }
  [[nodiscard]] bool isDropped() const { return dropped; }
  [[nodiscard]] bool isPrefetch() const {
    GI(prefetch, mt == mt_req || mt == mt_reqAck || mt == mt_disp);
    return prefetch;
  }

  // WARNING: Only available on DL1 demand miss
  [[nodiscard]] Dinst *getDinst() const { return dinst; }

  void setNeedsDisp() {
    I(mt == mt_setStateAck);
    needsDisp = true;
  }

  void     resetID(uint64_t _id) { id = _id; }
  uint64_t getID() { return id; }

  void destroy();

  void resetStart(MemObj *obj) {
    creatorObj = obj;
    homeMemObj = obj;
  }

  [[nodiscard]] MemObj *getHomeNode() const { return homeMemObj; }
  [[nodiscard]] MemObj *getCreator() const { return creatorObj; }
  [[nodiscard]] MemObj *getCurrMem() const { return currMemObj; }
  [[nodiscard]] MemObj *getPrevMem() const { return prevMemObj; }
  [[nodiscard]] bool    isHomeNode() const { return homeMemObj == currMemObj; }
  bool                  isHomeNodeSpec(MemObj *cache) const { return currMemObj == cache || cache->isLastLevelCache(); }

  [[nodiscard]] bool isTopCoherentNode() const {
    I(topCoherentNode);
    return topCoherentNode == currMemObj;
  }
  [[nodiscard]] MsgAction getAction() const { return ma; }
  [[nodiscard]] MsgAction getOrigAction() const { return ma_orig; }

  void trySetTopCoherentNode(MemObj *cache) {
    if (topCoherentNode == nullptr) {
      topCoherentNode = cache;
    }
  }

  [[nodiscard]] bool isMMU() const { return ma == ma_MMU; }
  [[nodiscard]] bool isVPCWriteUpdate() const { return ma == ma_VPCWU; }

  void ack() {
    if (cb) {
      if (dropped) {
        cb->destroy();
      } else {
        cb->call();
      }
    }
    if (mt == mt_setStateAck) {
      setStateAckDone(0);
    }

    dump_calledge(0);
    destroy();
  }
  void ack(TimeDelta_t lat) {
    I(lat);
    if (cb) {  // Not all the request require a completion notification
      cb->schedule(lat);
    }

    if (mt == mt_setStateAck) {
      setStateAckDone(lat);
    }

    dump_calledge(lat);
    destroy();
  }
  void ackAbs(Time_t when) {
    I(when);
    if (cb) {
      cb->scheduleAbs(when);
    }
    if (mt == mt_setStateAck) {
      setStateAckDone(when - globalClock);
    }

    dump_calledge(when - globalClock);
    destroy();
  }

  [[nodiscard]] Time_t getTimeDelay() const {
    I(startClock);
    return globalClock - startClock;
  }
  [[nodiscard]] Time_t getTimeDelay(Time_t when) const {
    I(startClock);
    I(startClock <= when);
    return when - startClock;
  }
  [[nodiscard]] Time_t getStartClock() const { return startClock; }

  [[nodiscard]] Addr_t getAddr() const { return addr; }

  [[nodiscard]] Addr_t getPC() const { return pc; }

  void setPC(Addr_t _pc) { pc = _pc; }

  [[nodiscard]] bool has_stats() const { return keep_stats; }
  [[nodiscard]] bool isWarmup() const { return warmup; }
  [[nodiscard]] bool isRetrying() const { return retrying; }
  void               setRetrying() { retrying = true; }
  void               clearRetrying() { retrying = false; }

  void               addPendingSetStateAck(MemRequest *mreq);
  [[nodiscard]] bool hasPendingSetStateAck() const { return pendingSetStateAck > 0; }
};
