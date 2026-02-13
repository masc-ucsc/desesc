// See LICENSE for details.

#pragma once

#include <iostream>
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
  Dinst* dinst{nullptr};
#ifdef DINST_PARENT
  Dinst* parentDinst{};
#endif
public:
  DinstNext() { dinst = nullptr; }

  DinstNext* nextDep;
  bool       isUsed;

  [[nodiscard]] const DinstNext* getNext() const { return nextDep; }
  [[nodiscard]] DinstNext*       getNext() { return nextDep; }

  void setNextDep(DinstNext* n) { nextDep = n; }

  void init(Dinst* d) {
    I(dinst == nullptr);
    dinst = d;
  }

  [[nodiscard]] Dinst* getDinst() const { return dinst; }

#ifdef DINST_PARENT
  [[nodiscard]] Dinst* getParentDinst() const { return parentDinst; }
  void                 setParentDinst(Dinst* d) {
    GI(d, isUsed);
    parentDinst = d;
  }
#else
  void setParentDinst(Dinst* d) {}
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
  DinstNext* last;
  DinstNext* first;

  Hartid_t fid;

  // BEGIN Boolean flags
  Time_t fetched;
  Time_t renamed;
  Time_t issued;
  Time_t executing;
  Time_t executed;

  bool branchMiss;
  bool use_level3;
  bool branch_hit2_miss3;
  bool branch_hit3_miss2;
  bool branchHit_level1;
  bool branchHit_level2;
  bool branchHit_level3;
  bool branchMiss_level1;
  bool branchMiss_level2;
  bool branchMiss_level3;
  bool level3_NoPrediction;

  bool retired;
  bool loadForwarded;
  bool replay;
  bool performed;

  bool interCluster;
  bool keep_stats;
  bool biasBranch;
  bool imli_highconf;

  bool prefetch;
  bool dispatched;
  bool fullMiss;
  bool speculative;
  bool transient;
  bool del_entry;
  bool is_rrob;
  bool present_in_rob;
  bool in_cluster;

  bool flush_transient;
  bool try_flush_transient;
  bool to_be_destroyed;
  bool destroy_transient;
  // END Boolean flags

  SSID_t      SSID;
  Addr_t      conflictStorePC;
  Instruction inst;
  Addr_t      pc;
  Addr_t      addr;
  uint64_t    inflight;
  int16_t     bb;

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
  Dinst**                   RAT1Entry;
  Dinst**                   RAT2Entry;
  Dinst**                   serializeEntry;
  FetchEngine*              fetch;
  GProcessor*               gproc;

  char nDeps;

  static inline Time_t currentID       = 0;
  static inline Time_t currentID_trans = 1000000;
  Time_t               ID;  // static ID, increased every create (currentID). pointer to the
#ifndef NDEBUG
  uint64_t mreq_id;
#endif
  void setup() {
    ID = currentID++;
#ifndef NDEBUG
    mreq_id = 0;
#endif
    first = nullptr;

    RAT1Entry      = nullptr;
    RAT2Entry      = nullptr;
    serializeEntry = nullptr;
    fetch          = nullptr;
    cluster        = nullptr;
    resource       = nullptr;

    gproc           = nullptr;
    SSID            = -1;
    conflictStorePC = 0;

    branchMiss          = false;
    use_level3          = false;
    branch_hit2_miss3   = false;
    branch_hit3_miss2   = false;
    branchHit_level1    = false;
    branchHit_level2    = false;
    branchHit_level3    = false;
    branchMiss_level1   = false;
    branchMiss_level2   = false;
    branchMiss_level3   = false;
    level3_NoPrediction = false;

    fetched   = 0;
    renamed   = 0;
    issued    = 0;
    executing = 0;
    executed  = 0;

    retired       = false;
    loadForwarded = false;
    replay        = false;
    performed     = false;

    interCluster  = false;
    biasBranch    = false;
    imli_highconf = false;

    prefetch       = false;
    dispatched     = false;
    fullMiss       = false;
    speculative    = true;
    transient      = false;
    del_entry      = false;
    is_rrob        = false;
    present_in_rob = false;
    in_cluster     = false;

    flush_transient     = false;
    try_flush_transient = false;
    to_be_destroyed     = false;
    destroy_transient   = false;

#ifdef DINST_PARENT
    pend[0].setParentDinst(nullptr);
    pend[1].setParentDinst(nullptr);
    pend[2].setParentDinst(nullptr);
#endif

    last  = nullptr;
    nDeps = 0;

    pend[0].isUsed = false;
    pend[1].isUsed = false;
    pend[2].isUsed = false;

    pend[0].setNextDep(nullptr);
    pend[1].setNextDep(nullptr);
    pend[2].setNextDep(nullptr);
  }

protected:
public:
  Dinst();

  [[nodiscard]] bool is_safe() const { return !speculative; }
  [[nodiscard]] bool is_spec() const { return speculative; }
  void               mark_safe() { speculative = false; }

  [[nodiscard]] bool isTransient() const { return transient; }
  void               setTransient() { transient = true; }
  void               mark_to_be_destroyed() { to_be_destroyed = true; }

  void clear_to_be_destroyed() { to_be_destroyed = false; }

  void mark_destroy_transient() { destroy_transient = true; }

  bool is_destroy_transient() { return to_be_destroyed; }

  void mark_del_entry() { del_entry = true; }

  void unmark_del_entry() { del_entry = false; }
  void mark_rrob() { is_rrob = true; }
  bool is_in_cluster() const { return in_cluster; }
  void set_in_cluster() { in_cluster = true; }

  void mark_flush_transient() { flush_transient = true; }
  void mark_try_flush_transient() { try_flush_transient = true; }

  bool is_present_in_rob() { return present_in_rob; }
  void set_present_in_rob() { present_in_rob = true; }
  bool is_flush_transient() { return flush_transient; }
  bool is_try_flush_transient() { return try_flush_transient; }
  bool has_stats() const { return keep_stats; }
  bool is_del_entry() { return del_entry; }
  bool is_present_rrob() { return is_rrob; }
  bool is_to_be_destroyed() { return to_be_destroyed; }

  [[nodiscard]] static Dinst* create(Instruction&& inst, Addr_t pc, Addr_t address, Hartid_t fid, bool keep_stats) {
    Dinst* i = dInstPool.out();
    I(inst.getOpcode() != Opcode::iOpInvalid);

    i->fid      = fid;
    i->inst     = std::move(inst);
    i->pc       = pc;
    i->addr     = address;
    i->inflight = 0;
    i->bb       = -1;
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
    i->brpc           = 0;
    i->delta          = 0;
    i->br_ld_chain    = false;
    i->br_op_type     = -1;
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

  [[nodiscard]] static DataSign calcDataSign(int64_t data);

  [[nodiscard]] int getDepDepth() const { return dep_depth; }

  void setDepDepth(int d) { dep_depth = d; }

  [[nodiscard]] int getLBType() const { return ld_br_type; }

  void setLBType(int lb) { ld_br_type = lb; }

  [[nodiscard]] Data_t getBrData1() const { return br_data1; }

  [[nodiscard]] Data_t getBrData2() const { return br_data2; }

  [[nodiscard]] Data_t getData() const { return data; }

  [[nodiscard]] Data_t getData2() const { return data2; }

  [[nodiscard]] DataSign getDataSign() const { return (DataSign)(int(data_sign) & 0x1FF); }

  // DataSign getDataSign() const { return data_sign; }
  void setDataSign(int64_t _data, Addr_t ldpc);
  void addDataSign(int ds, int64_t _data, Addr_t ldpc);

  void setBrData1(Data_t _data) { br_data1 = _data; }

  void setBrData2(Data_t _data) { br_data2 = _data; }

  void setData(uint64_t _data) { data = _data; }

  void setData2(uint64_t _data) { data2 = _data; }

  [[nodiscard]] Addr_t getLDPC() const { return ldpc; }
  void                 setChain(FetchEngine* fe, int c) {
    I(fetch == nullptr);
    I(c);
    I(fe);
    fetch   = fe;
    chained = c;
  }
  [[nodiscard]] int getChained() const { return chained; }
#else
  static DataSign calcDataSign([[maybe_unused]] int64_t data) { return DS_NoData; }
  Data_t          getData() const { return 0; }
  DataSign        getDataSign() const { return DS_NoData; }
  void            setDataSign([[maybe_unused]] int64_t _data, [[maybe_unused]] Addr_t ldpc) {}

  void   addDataSign([[maybe_unused]] int ds, [[maybe_unused]] int64_t _data, [[maybe_unused]] Addr_t ldpc) {}
  void   setData([[maybe_unused]] uint64_t _data) {}
  Addr_t getLDPC() const { return 0; }
  void   setChain([[maybe_unused]] FetchEngine* fe, [[maybe_unused]] int c) {}
  int    getChained() const { return 0; }

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
  [[nodiscard]] std::shared_ptr<Cluster>  getCluster() const { return cluster; }
  [[nodiscard]] std::shared_ptr<Resource> getClusterResource() const { return resource; }

  void clearRATEntry();
  void setRAT1Entry(Dinst** rentry) {
    I(!RAT1Entry);
    RAT1Entry = rentry;
  }
  void setRAT2Entry(Dinst** rentry) {
    I(!RAT2Entry);
    RAT2Entry = rentry;
  }
  void setSerializeEntry(Dinst** rentry) {
    I(!serializeEntry);
    serializeEntry = rentry;
  }

  void                 setSSID(SSID_t ssid) { SSID = ssid; }
  [[nodiscard]] SSID_t getSSID() const { return SSID; }

  void setConflictStorePC(Addr_t storepc) {
    I(storepc);
    I(this->getInst()->isLoad());
    conflictStorePC = storepc;
  }
  [[nodiscard]] Addr_t getConflictStorePC() const { return conflictStorePC; }

#ifdef DINST_PARENT
  Dinst* getParentSrc1() const {
    if (pend[0].isUsed) {
      return pend[0].getParentDinst();
    }
    return nullptr;
  }
  Dinst* getParentSrc2() const {
    if (pend[1].isUsed) {
      return pend[1].getParentDinst();
    }
    return nullptr;
  }
  Dinst* getParentSrc3() const {
    if (pend[2].isUsed) {  // true when RAW dependence
      return pend[2].getParentDinst();
    }
    return nullptr;
  }
#endif

  void lockFetch(FetchEngine* fe) {
    I(!branchMiss);
    I(fetch == nullptr);
    fetch      = fe;
    branchMiss = true;
    fetched    = globalClock;
  }

  void setFetchTime() {
#ifdef ESESC_TRACE_DATA
    I(fetch == nullptr || chained);
#else
    I(fetch == nullptr);
#endif
    I(!branchMiss);
    fetched = globalClock;
  }
  [[nodiscard]] int16_t getBB() const { return bb; }
  void                  setBB(int16_t b) { bb = b; }

  [[nodiscard]] uint64_t getInflight() const { return inflight; }

  void setInflight(uint64_t _inf) { inflight = _inf; }

  [[nodiscard]] bool isUseLevel3() const { return use_level3; }

  void setUseLevel3() { use_level3 = true; }

  void setBranch_hit2_miss3() { branch_hit2_miss3 = true; }
  void setBranch_hit3_miss2() { branch_hit3_miss2 = true; }

  [[nodiscard]] bool isBranch_hit2_miss3() const { return branch_hit2_miss3; }
  [[nodiscard]] bool isBranch_hit3_miss2() const { return branch_hit3_miss2; }

  void setBranchHit_level1() { branchHit_level1 = true; }
  void setBranchHit_level2() { branchHit_level2 = true; }
  void setBranchHit_level3() { branchHit_level3 = true; }

  [[nodiscard]] bool isBranchHit_level1() const { return branchHit_level1; }
  [[nodiscard]] bool isBranchHit_level2() const { return branchHit_level2; }
  [[nodiscard]] bool isBranchHit_level3() const { return branchHit_level3; }

  void setBranchMiss_level1() { branchMiss_level1 = true; }
  void setBranchMiss_level2() { branchMiss_level2 = true; }
  void setBranchMiss_level3() { branchMiss_level3 = true; }

  [[nodiscard]] bool isBranchMiss_level1() const { return branchMiss_level1; }
  [[nodiscard]] bool isBranchMiss_level2() const { return branchMiss_level2; }
  [[nodiscard]] bool isBranchMiss_level3() const { return branchMiss_level3; }

  void setLevel3_NoPrediction() { level3_NoPrediction = true; }

  [[nodiscard]] bool isLevel3_NoPrediction() const { return level3_NoPrediction; }

  [[nodiscard]] bool         isBranchMiss() const { return branchMiss; }
  [[nodiscard]] FetchEngine* getFetchEngine() const { return fetch; }

  Time_t getFetchTime() const { return fetched; }

  void setGProc(GProcessor* _gproc) {
    I(gproc == nullptr || gproc == _gproc);
    gproc = _gproc;
  }

  GProcessor* getGProc() const {
    I(gproc);
    return gproc;
  }

  Dinst* getNextPending() {
    I(first);
    Dinst* n = first->getDinst();
    I(n);

    I(n->nDeps > 0);
    n->nDeps--;
    first->isUsed = false;
    first->setParentDinst(nullptr);

    first = first->getNext();

    if (first) {
    } else {
      first = nullptr;
    }
    return n;
  }

  void addSrc1(Dinst* d) {
    I(d->nDeps < MAX_PENDING_SOURCES);

    d->nDeps++;

    I(executed == 0);
    I(d->executed == 0);
    DinstNext* n = &d->pend[0];
    I(!n->isUsed);
    n->isUsed = true;
    n->setParentDinst(this);

    I(n->getDinst() == d);
    if (first == nullptr) {
      first = n;
    } else {
      last->nextDep = n;
    }
    n->nextDep = nullptr;
    last       = n;
  }

  void addSrc2(Dinst* d) {
    I(d->nDeps < MAX_PENDING_SOURCES);

    d->nDeps++;
    I(executed == 0);
    I(d->executed == 0);

    DinstNext* n = &d->pend[1];
    I(!n->isUsed);
    n->isUsed = true;
    n->setParentDinst(this);

    I(n->getDinst() == d);
    if (first == nullptr) {
      first = n;
    } else {
      last->nextDep = n;
    }
    n->nextDep = nullptr;
    last       = n;
  }

  void addSrc3(Dinst* d) {
    I(d->nDeps < MAX_PENDING_SOURCES);
    d->nDeps++;
    I(executed == 0);
    I(d->executed == 0);

    DinstNext* n = &d->pend[2];
    I(!n->isUsed);
    n->isUsed = true;
    n->setParentDinst(this);

    I(n->getDinst() == d);
    if (first == nullptr) {
      first = n;
    } else {
      last->nextDep = n;
    }
    n->nextDep = nullptr;
    last       = n;
  }

  void     setPC(Addr_t a) { pc = a; }
  Addr_t   getPC() const { return pc; }
  Addr_t   getAddr() const { return addr; }
  Hartid_t getFlowId() const { return fid; }

  char getnDeps() const { return nDeps; }
  void decrease_deps() { nDeps--; }
  bool isSrc1Ready() const { return !pend[0].isUsed; }  // isUsed ==true ::RAW dependence
  bool isSrc2Ready() const { return !pend[1].isUsed; }
  bool isSrc3Ready() const { return !pend[2].isUsed; }
  void flush_first() { first = nullptr; }
  bool hasPending() const {
    GI(!pend[0].isUsed && !pend[1].isUsed && !pend[2].isUsed, nDeps == 0);
    return first != nullptr;
  }

  bool hasDeps() const {
    if (!isTransient()) {
      GI(!pend[0].isUsed && !pend[1].isUsed && !pend[2].isUsed, nDeps == 0);
    }
    return nDeps != 0;
  }

  const Dinst*     getFirstPending() const { return first->getDinst(); }
  const DinstNext* getFirst() const { return first; }

  const Instruction* getInst() const { return &inst; }

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

  void markIssuedTransient() { issued = globalClock; }

  bool isExecuted() const { return executed; }
  void markExecuted() {
    I(issued != 0);
    I(executed == 0);
    executed = globalClock;
  }
  void markExecutedTransient() { executed = globalClock; }

  bool isExecuting() const { return executing; }
  void markExecuting() {
    I(issued != 0);
    I(executing == 0);
    executing = globalClock;
  }
  void markExecutingTransient() { executing = globalClock; }

  bool isReplay() const { return replay; }
  void markReplay() { replay = true; }

  void setBiasBranch(bool b) { biasBranch = b; }
  bool isBiasBranch() const { return biasBranch; }

  void setImliHighConf() { imli_highconf = true; }

  bool getImliHighconf() const { return imli_highconf; }

  bool isTaken() const {
    I(getInst()->isControl());
    return addr != 0;
  }

  bool isPerformed() const { return performed; }
  void markPerformed() {
    if (!this->isTransient()) {
      GI(!inst.isLoad(), executed != 0);
    }

    performed = true;
  }

  bool isRetired() const { return retired; }
  void markRetired() {
    I(inst.isStore());
    retired = true;
  }
  void mark_retired() { retired = true; }

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
  size_t operator()(const Dinst* dinst) const { return (size_t)(dinst); }
};
