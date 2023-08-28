// See LICENSE for details.

#pragma once

#include <memory>

#include "callback.hpp"
#include "iassert.hpp"
#include "instruction.hpp"
#include "opcode.hpp"
#include "pool.hpp"
#include "snippets.hpp"

using SSID_t = int32_t;

class Dinst;
class FetchEngine;
class BPredictor;
class Cluster;
class Resource;
class GProcessor;

// #define ESESC_TRACE 1
#define DINST_PARENT

class DinstNext {
private:
  Dinst *dinst;
#ifdef DINST_PARENT
  Dinst *parentDinst;
#endif
public:
  DinstNext() { dinst = 0; }

  DinstNext *nextDep;
  bool       isUsed;  // true while non-satisfied RAW dependence

  const DinstNext *getNext() const { return nextDep; }
  DinstNext       *getNext() { return nextDep; }

  void setNextDep(DinstNext *n) { nextDep = n; }

  void init(Dinst *d) {
    I(dinst == 0);
    dinst = d;
  }

  Dinst *getDinst() const { return dinst; }

#ifdef DINST_PARENT
  Dinst *getParentDinst() const { return parentDinst; }
  void   setParentDinst(Dinst *d) {
    GI(d, isUsed);
    parentDinst = d;
  }
#else
  void            setParentDinst(Dinst *d) {}
#endif
};

enum DataSign {
  DS_NoData = 0,
  DS_V0     = 1,
  DS_P1     = 2,
  DS_P2     = 3,
  DS_P3     = 4,
  DS_P4     = 5,
  DS_P5     = 6,
  DS_P6     = 7,
  DS_P7     = 8,
  DS_P8     = 9,
  DS_P9     = 10,
  DS_P10    = 11,
  DS_P11    = 12,
  DS_P12    = 13,
  DS_P13    = 14,
  DS_P14    = 15,
  DS_P15    = 16,
  DS_P16    = 17,
  DS_P32    = 18,
  DS_N1     = 19,
  DS_N2     = 20,
  DS_ONeg   = 21,
  DS_EQ     = 22,
  DS_GTEZ   = 23,
  DS_LTC    = 24,
  DS_GEC    = 25,
  DS_LTZC   = 26,
  DS_LEZC   = 27,
  DS_GTZC   = 28,
  DS_GEZC   = 29,
  DS_EQZC   = 30,
  DS_NEZC   = 31,
  DS_GTZ    = 32,
  DS_LEZ    = 33,
  DS_LTZ    = 34,
  DS_NE     = 35,
  DS_PTR    = 36,
  DS_FIVE   = 37,  // factor of 5
  DS_POW    = 38,  // n is 2^n
  DS_MOD    = 39,  // n%255 + (DS_V0...DS_N2 or DS_FIVE or DS_POW)
  DS_OPos   = 40
};

class Dinst {
private:
  // In a typical RISC processor MAX_PENDING_SOURCES should be 2
  static const int32_t MAX_PENDING_SOURCES = 3;

  static pool<Dinst> dInstPool;

  DinstNext  pend[MAX_PENDING_SOURCES];
  DinstNext *last;
  DinstNext *first;

  Hartid_t fid;

  // BEGIN Boolean flags
  Time_t fetched;
  Time_t renamed;
  Time_t issued;
  Time_t executing;
  Time_t executed;

  bool retired;

  bool loadForwarded;
  bool replay;
  bool branchMiss;
  bool use_level3;         // use level3 bpred or not?
  bool branch_hit2_miss3;  // coorect pred by level 2 BP but misprediction by level 3 BP
  bool branch_hit3_miss2;  // coorect pred by level 3 BP but misprediction by level 2 BP
  bool branchHit_level1;
  bool branchHit_level2;
  bool branchHit_level3;
  bool branchMiss_level1;
  bool branchMiss_level2;
  bool branchMiss_level3;
  bool level3_NoPrediction;
  int  trig_ld_status;  // TL timeliness, (-1)->no LDBP; 0->on time; 1->late
  bool trig_ld1_pred;
  bool trig_ld1_unpred;
  bool trig_ld2_pred;
  bool trig_ld2_unpred;

  bool performed;

  bool     interCluster;
  bool     keep_stats;
  bool     biasBranch;
  uint32_t branch_signature;
  bool     imli_highconf;

  bool prefetch;
  bool dispatched;
  bool fullMiss;  // Only for DL1
  bool speculative;
  bool transient;
  // END Boolean flags

  SSID_t      SSID;
  Addr_t      conflictStorePC;
  Instruction inst;
  Addr_t      pc;    // PC for the dinst
  Addr_t      addr;  // Either load/store address or jump/branch address
  uint64_t    inflight;

#ifdef ESESC_TRACE_DATA
  Addr_t   ldpc;
  Addr_t   ld_addr;
  Addr_t   base_pref_addr;
  Data_t   data;
  Data_t   data2;
  DataSign data_sign;
  Data_t   br_data1;
  Data_t   br_data2;
  int      ld_br_type;
  int      dep_depth;
  int      chained;
  // BR stats
  Addr_t   brpc;
  uint64_t delta;
  uint64_t br_op_type;
  int      ret_br_count;
  bool     br_ld_chain_predictable;
  bool     br_ld_chain;
#endif

  std::shared_ptr<Cluster>  cluster;
  std::shared_ptr<Resource> resource;
  Dinst                   **RAT1Entry;
  Dinst                   **RAT2Entry;
  Dinst                   **serializeEntry;
  FetchEngine              *fetch;
  GProcessor               *gproc;

  char nDeps;  // 0, 1 or 2 for RISC processors

  static inline Time_t currentID = 0;
  Time_t               ID;  // static ID, increased every create (currentID). pointer to the
#ifndef NDEBUG
  uint64_t mreq_id;
#endif
  void setup() {
    ID = currentID++;
#ifndef NDEBUG
    mreq_id = 0;
#endif
    first = 0;

    RAT1Entry           = 0;
    RAT2Entry           = 0;
    serializeEntry      = 0;
    fetch               = 0;
    cluster             = nullptr;
    resource            = nullptr;
    branchMiss          = false;
    use_level3          = false;
    trig_ld1_pred       = false;
    trig_ld1_unpred     = false;
    trig_ld2_pred       = false;
    trig_ld2_unpred     = false;
    branch_hit2_miss3   = false;
    branch_hit3_miss2   = false;
    branchHit_level1    = false;
    branchHit_level2    = false;
    branchHit_level3    = false;
    branchMiss_level1   = false;
    branchMiss_level2   = false;
    branchMiss_level3   = false;
    level3_NoPrediction = false;
    trig_ld_status      = -1;
    imli_highconf       = false;
    gproc               = 0;
    SSID                = -1;
    conflictStorePC     = 0;

    fetched   = 0;
    renamed   = 0;
    issued    = 0;
    executing = 0;
    executed  = 0;
    retired   = false;

    loadForwarded = false;

    replay       = false;
    performed    = false;
    interCluster = false;
    prefetch     = false;
    dispatched   = false;
    fullMiss     = false;
    speculative  = true;
    transient    = false;

#ifdef DINST_PARENT
    pend[0].setParentDinst(0);
    pend[1].setParentDinst(0);
    pend[2].setParentDinst(0);
#endif

    pend[0].isUsed = false;
    pend[1].isUsed = false;
    pend[2].isUsed = false;
  }

protected:
public:
  Dinst();
#if 0
  bool   isSpec       = false;
  bool   isSafe       = false;
  bool   isLdCache    = false;
  Time_t memReqTimeL1 = 0;
#endif

  bool is_safe() const { return !speculative; }
  bool is_spec() const { return speculative; }
  void mark_safe() { speculative = false; }
  
  bool isTransient() const { 
    //printf("checking transient Inst %B", transient);
    return transient;
  }
  void setTransient() { 
    transient = true; 
    //printf("Setting transient in ::dinst\n");
  }

  bool has_stats() const { return keep_stats; }

#if 0
  void setLdCache() { isLdCache = true; }
  bool getLdCache() { return isLdCache; }
  void resetLdCache() { isLdCache = false; }

  void setMemReqTimeL1(Time_t time) {
    // printf("DINST::seting time for L1 req  at time %lld\n",time);
    memReqTimeL1 = time;
  }
  Time_t        getMemReqTimeL1() { return memReqTimeL1; }

  void          setSpec() { isSpec = true; }
  void          clearSpec() { isSpec = false; }
  void          setSafe() { isSafe = true; }
  void          clearSafe() { isSafe = false; }
#endif

  static Dinst *create(Instruction &&inst, Addr_t pc, Addr_t address, Hartid_t fid, bool keep_stats) {
    Dinst *i = dInstPool.out();
    I(inst.getOpcode() != Opcode::iOpInvalid);

    i->fid      = fid;
    i->inst     = std::move(inst);
    i->pc       = pc;
    i->addr     = address;
    i->inflight = 0;
#ifdef ESESC_TRACE_DATA
    i->data           = 0;
    i->data2          = 0;
    i->br_data1       = 0;
    i->br_data2       = 0;
    i->ld_br_type     = 0;
    i->dep_depth      = 0;
    i->ldpc           = 0;
    i->ld_addr        = 0;
    i->base_pref_addr = 0;
    i->data_sign      = DS_NoData;
    i->chained        = 0;
    // BR stats
    i->brpc        = 0;
    i->delta       = 0;
    i->br_ld_chain = false;
    i->br_op_type  = -1;
#endif
    i->fetched    = 0;
    i->keep_stats = keep_stats;

    i->setup();
    I(i->getInst()->getOpcode() != Opcode::iOpInvalid);

    return i;
  }
#ifdef ESESC_TRACE_DATA
  uint64_t getDelta() const { return delta; }

  void setDelta(uint64_t _delta) { delta = _delta; }

  int getRetireBrCount() const { return ret_br_count; }

  void setRetireBrCount(int _cnt) { ret_br_count = _cnt; }

  bool is_br_ld_chain() const { return br_ld_chain; }

  void set_br_ld_chain() { br_ld_chain = true; }

  bool is_br_ld_chain_predictable() { return br_ld_chain_predictable; }

  void set_br_ld_chain_predictable() { br_ld_chain_predictable = true; }

  Addr_t getBasePrefAddr() const { return base_pref_addr; }

  void setBasePrefAddr(Addr_t _base_addr) { base_pref_addr = _base_addr; }

  Addr_t getLdAddr() const { return ld_addr; }

  void setLdAddr(Addr_t _ld_addr) { ld_addr = _ld_addr; }

  Addr_t getBrPC() const { return brpc; }

  void setBrPC(Addr_t _brpc) { brpc = _brpc; }

  static DataSign calcDataSign(int64_t data);

  int getDepDepth() const { return dep_depth; }

  void setDepDepth(int d) { dep_depth = d; }

  int getLBType() const { return ld_br_type; }

  void setLBType(int lb) { ld_br_type = lb; }

  Data_t getBrData1() const { return br_data1; }

  Data_t getBrData2() const { return br_data2; }

  Data_t getData() const { return data; }

  Data_t getData2() const { return data2; }

  DataSign getDataSign() const { return (DataSign)(int(data_sign) & 0x1FF); }  // FIXME:}

  // DataSign getDataSign() const { return data_sign; }
  void setDataSign(int64_t _data, Addr_t ldpc);
  void addDataSign(int ds, int64_t _data, Addr_t ldpc);

  void setBrData1(Data_t _data) { br_data1 = _data; }

  void setBrData2(Data_t _data) { br_data2 = _data; }

  void setData(uint64_t _data) { data = _data; }

  void setData2(uint64_t _data) { data2 = _data; }

  Addr_t getLDPC() const { return ldpc; }
  void   setChain(FetchEngine *fe, int c) {
    I(fetch == 0);
    I(c);
    I(fe);
    fetch   = fe;
    chained = c;
  }
  int getChained() const { return chained; }
#else
  static DataSign calcDataSign(int64_t data) {
    (void)data;
    return DS_NoData;
  };
  Data_t   getData() const { return 0; }
  DataSign getDataSign() const { return DS_NoData; }
  void     setDataSign(int64_t _data, Addr_t ldpc) {
    (void)_data;
    (void)ldpc;
  }

  void addDataSign(int ds, int64_t _data, Addr_t ldpc) {
    (void)ds;
    (void)_data;
    (void)ldpc;
  }
  void   setData(uint64_t _data) { (void)_data; }
  Addr_t getLDPC() const { return 0; }
  void   setChain(FetchEngine *fe, int c) {
    (void)fe;
    (void)c;
  }
  int getChained() const { return 0; }

  int getDepDepth() const { return 0; }

  int getLBType() { return 0; }

  Data_t getBrData1() const { return 0; }

  Data_t getBrData2() const { return 0; }

  Data_t getData2() const { return 0; }
#endif

  void scrap();  // Destroys the instruction without any other effects
  void destroy();
  void destroyTransientInst();


  void set(std::shared_ptr<Cluster> cls, std::shared_ptr<Resource> res) {
    cluster  = cls;
    resource = res;
  }
  std::shared_ptr<Cluster>  getCluster() const { return cluster; }
  std::shared_ptr<Resource> getClusterResource() const { return resource; }

  void clearRATEntry();
  void setRAT1Entry(Dinst **rentry) {
    I(!RAT1Entry);
    RAT1Entry = rentry;
  }
  void setRAT2Entry(Dinst **rentry) {
    I(!RAT2Entry);
    RAT2Entry = rentry;
  }
  void setSerializeEntry(Dinst **rentry) {
    I(!serializeEntry);
    serializeEntry = rentry;
  }

  void   setSSID(SSID_t ssid) { SSID = ssid; }
  SSID_t getSSID() const { return SSID; }

  void setConflictStorePC(Addr_t storepc) {
    I(storepc);
    I(this->getInst()->isLoad());
    conflictStorePC = storepc;
  }
  Addr_t getConflictStorePC() const { return conflictStorePC; }

#ifdef DINST_PARENT
  Dinst *getParentSrc1() const {
    if (pend[0].isUsed) {
      return pend[0].getParentDinst();
    }
    return 0;
  }
  Dinst *getParentSrc2() const {
    if (pend[1].isUsed) {
      return pend[1].getParentDinst();
    }
    return 0;
  }
  Dinst *getParentSrc3() const {
    if (pend[2].isUsed) {
      return pend[2].getParentDinst();
    }
    return 0;
  }
#endif

  void lockFetch(FetchEngine *fe) {
    I(!branchMiss);
    I(fetch == 0);
    fetch      = fe;
    branchMiss = true;
    fetched    = globalClock;
  }

  void setFetchTime() {
#ifdef ESESC_TRACE_DATA
    I(fetch == 0 || chained);
#else
    I(fetch == 0);
#endif
    I(!branchMiss);
    fetched = globalClock;
  }

  uint64_t getInflight() const { return inflight; }

  void setInflight(uint64_t _inf) { inflight = _inf; }

  void setUseLevel3() { use_level3 = true; }

  bool isUseLevel3() const { return use_level3; }

  void setTrig_ld1_pred() { trig_ld1_pred = true; }

  bool isTrig_ld1_pred() const { return trig_ld1_pred; }

  void setTrig_ld1_unpred() { trig_ld1_unpred = true; }

  bool isTrig_ld1_unpred() const { return trig_ld1_unpred; }

  void setTrig_ld2_pred() { trig_ld2_pred = true; }

  bool isTrig_ld2_pred() const { return trig_ld2_pred; }

  void setTrig_ld2_unpred() { trig_ld2_unpred = true; }

  bool isTrig_ld2_unpred() const { return trig_ld2_unpred; }

  void setBranch_hit2_miss3() { branch_hit2_miss3 = true; }

  bool isBranch_hit2_miss3() const { return branch_hit2_miss3; }

  void setBranch_hit3_miss2() { branch_hit3_miss2 = true; }

  bool isBranch_hit3_miss2() const { return branch_hit3_miss2; }

  void setBranchHit_level1() { branchHit_level1 = true; }

  bool isBranchHit_level1() const { return branchHit_level1; }

  void setBranchHit_level2() { branchHit_level2 = true; }

  bool isBranchHit_level2() const { return branchHit_level2; }

  void setBranchHit_level3() { branchHit_level3 = true; }

  bool isBranchHit_level3() const { return branchHit_level3; }

  void setBranchMiss_level1() { branchMiss_level1 = true; }

  bool isBranchMiss_level1() const { return branchMiss_level1; }

  void setBranchMiss_level2() { branchMiss_level2 = true; }

  bool isBranchMiss_level2() const { return branchMiss_level2; }

  void setBranchMiss_level3() { branchMiss_level3 = true; }

  bool isBranchMiss_level3() const { return branchMiss_level3; }

  void set_trig_ld_status() {  // set to 0
    if (trig_ld_status == -1) {
      trig_ld_status = 0;
    }
  }

  void inc_trig_ld_status() {  // inc on late TL
    if (trig_ld_status == 0) {
      trig_ld_status = 1;
    }
  }

  int get_trig_ld_status() const { return trig_ld_status; }

  void setLevel3_NoPrediction() { level3_NoPrediction = true; }

  bool isLevel3_NoPrediction() const { return level3_NoPrediction; }

  bool         isBranchMiss() const { return branchMiss; }
  FetchEngine *getFetchEngine() const { return fetch; }

  Time_t getFetchTime() const { return fetched; }

  void setGProc(GProcessor *_gproc) {
    I(gproc == 0 || gproc == _gproc);
    gproc = _gproc;
  }

  GProcessor *getGProc() const {
    I(gproc);
    return gproc;
  }

  Dinst *getNextPending() {
    I(first);
    Dinst *n = first->getDinst();

    I(n);

    I(n->nDeps > 0);
    n->nDeps--;

    first->isUsed = false;
    first->setParentDinst(0);
    first = first->getNext();

    return n;
  }

  void addSrc1(Dinst *d) {
    I(d->nDeps < MAX_PENDING_SOURCES);
    d->nDeps++;

    I(executed == 0);
    I(d->executed == 0);
    DinstNext *n = &d->pend[0];
    I(!n->isUsed);
    n->isUsed = true;
    n->setParentDinst(this);

    I(n->getDinst() == d);
    if (first == 0) {
      first = n;
    } else {
      last->nextDep = n;
    }
    n->nextDep = 0;
    last       = n;
  }

  void addSrc2(Dinst *d) {
    I(d->nDeps < MAX_PENDING_SOURCES);
    d->nDeps++;
    I(executed == 0);
    I(d->executed == 0);

    DinstNext *n = &d->pend[1];
    I(!n->isUsed);
    n->isUsed = true;
    n->setParentDinst(this);

    I(n->getDinst() == d);
    if (first == 0) {
      first = n;
    } else {
      last->nextDep = n;
    }
    n->nextDep = 0;
    last       = n;
  }

  void addSrc3(Dinst *d) {
    I(d->nDeps < MAX_PENDING_SOURCES);
    d->nDeps++;
    I(executed == 0);
    I(d->executed == 0);

    DinstNext *n = &d->pend[2];
    I(!n->isUsed);
    n->isUsed = true;
    n->setParentDinst(this);

    I(n->getDinst() == d);
    if (first == 0) {
      first = n;
    } else {
      last->nextDep = n;
    }
    n->nextDep = 0;
    last       = n;
  }

#if 0
  void setAddr(Addr_t a) {
    addr = a;
  }
#endif
  void     setPC(Addr_t a) { pc = a; }
  Addr_t   getPC() const { return pc; }
  Addr_t   getAddr() const { return addr; }
  Hartid_t getFlowId() const { return fid; }

  char getnDeps() const { return nDeps; }
  bool isSrc1Ready() const { return !pend[0].isUsed; }
  bool isSrc2Ready() const { return !pend[1].isUsed; }
  bool isSrc3Ready() const { return !pend[2].isUsed; }
  bool hasPending() const { return first != 0; }

  bool hasDeps() const {
    GI(!pend[0].isUsed && !pend[1].isUsed && !pend[2].isUsed, nDeps == 0);
    return nDeps != 0;
  }

  const Dinst     *getFirstPending() const { return first->getDinst(); }
  const DinstNext *getFirst() const { return first; }

  const Instruction *getInst() const { return &inst; }

  void dump(std::string_view txt);

  // methods required for LDSTBuffer
  bool isLoadForwarded() const { return loadForwarded; }
  void setLoadForwarded() {
    I(!loadForwarded);
    loadForwarded = true;
  }

  bool hasInterCluster() const { return interCluster; }
  void markInterCluster() { interCluster = true; }

  bool isIssued() const { return issued; }

  void markRenamed() {
    I(renamed == 0);
    renamed = globalClock;
  }
  bool isRenamed() const { return renamed != 0; }

  void markIssued() {
    I(issued == 0);
    I(executing == 0);
    I(executed == 0);
    issued = globalClock;
  }

  void markIssuedTransient() {
   // I(issued == 0);
    //I(executing == 0);
    //I(executed == 0);
    issued = globalClock;
  }


  bool isExecuted() const { return executed; }
  void markExecuted() {
    I(issued != 0);
    I(executed == 0);
    executed = globalClock;
  }
  void markExecutedTransient() {
   // I(issued != 0);
    //I(executed == 0);
    executed = globalClock;
  }


  bool isExecuting() const { return executing; }
  void markExecuting() {
    I(issued != 0);
    I(executing == 0);
    executing = globalClock;
  }
  void markExecutingTransient() {
   // I(issued != 0);
    //I(executing == 0);
    executing = globalClock;
  }

  bool isReplay() const { return replay; }
  void markReplay() { replay = true; }

  void setBiasBranch(bool b) { biasBranch = b; }
  bool isBiasBranch() const { return biasBranch; }

  void setImliHighConf() { imli_highconf = true; }

  bool getImliHighconf() const { return imli_highconf; }

  void     setBranchSignature(uint32_t s) { branch_signature = s; }
  uint32_t getBranchSignature() const { return branch_signature; }

  bool isTaken() const {
    I(getInst()->isControl());
    return addr != 0;
  }

  bool isPerformed() const { return performed; }
  void markPerformed() {
    // Loads get performed first, and then executed
    GI(!inst.isLoad(), executed != 0);
    performed = true;
  }

  bool isRetired() const { return retired; }
  void markRetired() {
    I(inst.isStore());
    retired = true;
  }
  bool isPrefetch() const { return prefetch; }
  void markPrefetch() { prefetch = true; }
  bool isDispatched() const { return dispatched; }
  void markDispatched() { dispatched = true; }
  bool isFullMiss() const { return fullMiss; }
  void setFullMiss(bool t) { fullMiss = t; }

  Time_t getFetchedTime() const { return fetched; }
  Time_t getRenamedTime() const { return renamed; }
  Time_t getIssuedTime() const { return issued; }
  Time_t getExecutingTime() const { return executing; }
  Time_t getExecutedTime() const { return executed; }

  Time_t getID() const { return ID; }

#ifndef NDEBUG
  uint64_t getmreq_id() { return mreq_id; }
  void     setmreq_id(uint64_t _mreq_id) { mreq_id = _mreq_id; }
#endif
};

class Hash4Dinst {
public:
  size_t operator()(const Dinst *dinst) const { return (size_t)(dinst); }
};
