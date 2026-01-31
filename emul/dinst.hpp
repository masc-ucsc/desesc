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
  DinstNext() { dinst = 0; }

  DinstNext* nextDep;
  bool       isUsed;  // true while non-satisfied RAW dependence
                      // fasle when no RAW dependence
                      // true  when RAW dependence
                      /*=======
                        DinstNext() = default;
                    
                        DinstNext *nextDep{};
                        bool       isUsed{};  // true while non-satisfied RAW dependence
                      >>>>>>> upstream/main*/

  const DinstNext* getNext() const { return nextDep; }
  DinstNext*       getNext() { return nextDep; }

  void setNextDep(DinstNext* n) { nextDep = n; }

  void init(Dinst* d) {
    I(dinst == 0);
    dinst = d;
  }

  Dinst* getDinst() const { return dinst; }
  // void  set_dinst(Dinst *d) {  dinst = d; }

#ifdef DINST_PARENT
  Dinst* getParentDinst() const { return parentDinst; }
  void   setParentDinst(Dinst* d) {
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

  bool retired;
  bool loadForwarded;
  bool replay;
  bool performed;

  bool interCluster;
  bool keep_stats;
  bool biasBranch;
  bool zero_delay_taken;
  bool imli_highconf;

  bool prefetch;
  bool dispatched;
  bool fullMiss;  // Only for DL1
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
  Addr_t      pc;    // PC for the dinst
  Addr_t      addr;  // Either load/store address or jump/branch address
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

  char nDeps;  // 0, 1 or 2 for RISC processors

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

    interCluster = false;
    // keep_stats - is an argument
    biasBranch       = false;
    zero_delay_taken = false;
    imli_highconf    = false;

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
    pend[0].setParentDinst(0);
    pend[1].setParentDinst(0);
    pend[2].setParentDinst(0);
#endif

    first          = nullptr;
    last           = 0;
    nDeps          = 0;
    pend[0].isUsed = false;  // false when no RAW dependence
    pend[1].isUsed = false;  // true when RAW dependence
    pend[2].isUsed = false;

    pend[0].setNextDep(0);
    pend[1].setNextDep(0);
    pend[2].setNextDep(0);

    // last->setNextDep(0);
    // first->setNextDep(0);
    // first->init(0);
    // pend[1].init(0);
    // pend[2].init(0);
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

  bool isTransient() const { return transient; }
  void setTransient() {
    transient = true;
    // ID = currentID_trans++;
  }
  void mark_to_be_destroyed() {
    to_be_destroyed = true;
    // printf("Setting mark_to_be_destroyed_transient ::dinst %ld\n", this->ID);
  }

  void clear_to_be_destroyed() {
    to_be_destroyed = false;
    // printf("Clearing is_to_be_destroyed_transient to false ::dinst %ld\n", ID);
  }

  void mark_destroy_transient() {
    destroy_transient = true;
    // printf("Setting mark_to_be_destroyed_transient ::dinst %ld\n", this->ID);
  }

  bool is_destroy_transient() {
    return to_be_destroyed;
    // printf("Clearing is_to_be_destroyed_transient to false ::dinst %ld\n", ID);
  }

  void mark_del_entry() {
    del_entry = true;
    // printf("Setting mark_del_entry  ::dinst %ld\n", ID);
  }

  void unmark_del_entry() {
    del_entry = false;
    // printf("Setting mark_del_entry  ::dinst %ld\n", ID);
  }
  void mark_rrob() {
    is_rrob = true;
    // printf("Setting mark_rrob  ::dinst %ld\n", ID);
  }
  bool is_in_cluster() const {
    // printf("checking transient Inst %B", transient);
    return in_cluster;
  }
  void set_in_cluster() {
    in_cluster = true;
    // ID = currentID_trans++;

    // printf("Setting  incluster true ::dinst %ld\n", ID);
  }

  //=======
  void mark_flush_transient() { flush_transient = true; }
  void mark_try_flush_transient() { try_flush_transient = true; }

  // void mark_del_entry() { del_entry = true; }

  // void unmark_del_entry() { del_entry = false; }
  // void mark_rrob() { is_rrob = true; }

  bool is_present_in_rob() { return present_in_rob; }
  void set_present_in_rob() { present_in_rob = true; }
  bool is_flush_transient() { return flush_transient; }
  bool is_try_flush_transient() { return try_flush_transient; }
  bool has_stats() const { return keep_stats; }
  bool is_del_entry() { return del_entry; }
  bool is_present_rrob() { return is_rrob; }
  bool is_to_be_destroyed() { return to_be_destroyed; }
  //=======

  static Dinst* create(Instruction&& inst, Addr_t pc, Addr_t address, Hartid_t fid, bool keep_stats) {
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
  void   setChain(FetchEngine* fe, int c) {
    I(fetch == 0);
    I(c);
    I(fe);
    fetch   = fe;
    chained = c;
  }
  int getChained() const { return chained; }
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
  std::shared_ptr<Cluster>  getCluster() const { return cluster; }
  std::shared_ptr<Resource> getClusterResource() const { return resource; }

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

  void   setSSID(SSID_t ssid) { SSID = ssid; }
  SSID_t getSSID() const { return SSID; }

  void setConflictStorePC(Addr_t storepc) {
    I(storepc);
    I(this->getInst()->isLoad());
    conflictStorePC = storepc;
  }
  Addr_t getConflictStorePC() const { return conflictStorePC; }

#ifdef DINST_PARENT
  Dinst* getParentSrc1() const {
    if (pend[0].isUsed) {  // true when RAW dependence
      // printf("Dinst::getparentsrc1:parent inst src1 is %ld and current Inst isTransient is %b\n",
      //        pend[0].getParentDinst()->getID(),
      //        isTransient());
      // std::cout << "Dinst::getparentsrc1:: parentscr1 asm is " << pend[0].getParentDinst()->getInst()->get_asm() << std::endl;
      return pend[0].getParentDinst();
    }
    return 0;
  }
  Dinst* getParentSrc2() const {
    if (pend[1].isUsed) {  // true when RAW dependence
      // printf("Dinst::getparentsrc2:Inst is %ld and isTransient is %b\n", pend[1].getParentDinst()->getID(), isTransient());
      // std::cout << "Dinst::getparentsrc2:: asm is " << pend[1].getParentDinst()->getInst()->get_asm() << std::endl;
      return pend[1].getParentDinst();
    }
    return 0;
  }
  Dinst* getParentSrc3() const {
    if (pend[2].isUsed) {  // true when RAW dependence
      return pend[2].getParentDinst();
    }
    return 0;
  }
#endif

  void lockFetch(FetchEngine* fe) {
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
  int16_t getBB() const { return bb; }
  void    setBB(int16_t b) { bb = b; }

  uint64_t getInflight() const { return inflight; }

  void setInflight(uint64_t _inf) { inflight = _inf; }

  bool isUseLevel3() const { return use_level3; }

  void setUseLevel3() { use_level3 = true; }

  void setBranch_hit2_miss3() { branch_hit2_miss3 = true; }
  void setBranch_hit3_miss2() { branch_hit3_miss2 = true; }

  bool isBranch_hit2_miss3() const { return branch_hit2_miss3; }
  bool isBranch_hit3_miss2() const { return branch_hit3_miss2; }

  void setBranchHit_level1() { branchHit_level1 = true; }
  void setBranchHit_level2() { branchHit_level2 = true; }
  void setBranchHit_level3() { branchHit_level3 = true; }

  bool isBranchHit_level1() const { return branchHit_level1; }
  bool isBranchHit_level2() const { return branchHit_level2; }
  bool isBranchHit_level3() const { return branchHit_level3; }

  void setBranchMiss_level1() { branchMiss_level1 = true; }
  void setBranchMiss_level2() { branchMiss_level2 = true; }
  void setBranchMiss_level3() { branchMiss_level3 = true; }

  bool isBranchMiss_level1() const { return branchMiss_level1; }
  bool isBranchMiss_level2() const { return branchMiss_level2; }
  bool isBranchMiss_level3() const { return branchMiss_level3; }

  void setLevel3_NoPrediction() { level3_NoPrediction = true; }

  bool isLevel3_NoPrediction() const { return level3_NoPrediction; }

  bool         isBranchMiss() const { return branchMiss; }
  FetchEngine* getFetchEngine() const { return fetch; }

  Time_t getFetchTime() const { return fetched; }

  void setGProc(GProcessor* _gproc) {
    I(gproc == 0 || gproc == _gproc);
    gproc = _gproc;
  }

  GProcessor* getGProc() const {
    I(gproc);
    return gproc;
  }

  Dinst* getNextPending() {
    I(first);
    Dinst* n = first->getDinst();

    // printf("Dinst::getnextPending :: current inst is %ld and isTransient is %b\n", this->getID(), this->isTransient());
    // std::cout << "Dinst::getNextPending:: current inst ::asm is " << this->getInst()->get_asm() << std::endl;
    // printf("Dinst::getnextPending :: pending inst is %ld and isTransient is %b\n", n->getID(), n->isTransient());
    // std::cout << "Dinst::getNextPending::first->getDinst():: pending inst ::asm is " << n->getInst()->get_asm() << std::endl;
    I(n);

    // printf("Dinst::getNextPending:: Before ndeps is: first->getDinst()->ndeps is %d\n", (int)n->getnDeps());
    I(n->nDeps > 0);
    n->nDeps--;
    // printf("Dinst::getNextPending::Now ndeps--:: ndeps is:first->getDinst()->ndeps-- is  %d\n", (int)n->getnDeps());
    first->isUsed = false;           // isUsed==false : No RAW dependence
    first->setParentDinst(nullptr);  // setParent =nullptr ::reset

    first = first->getNext();  // first <=
    // I(first);

    if (first) {
      // printf("Dinst::getnextPending Setting new first as ::inst is %ld and isTransient is %b\n",
      //        first->getDinst()->getID(),
      //        first->getDinst()->isTransient());
    } else {
      first = nullptr;
      // printf("Dinst::getnextPending Setting new first =0 as ::inst is %ld and isTransient is %b\n",
      //        this->getID(),
      //        this->isTransient());
    }
    return n;
  }

  void addSrc1(Dinst* d) {
    I(d->nDeps < MAX_PENDING_SOURCES);

    // printf("Entering Dinst::addSrc1::Current RAT Inst is %ld and isTransient is %b\n", getID(), isTransient());
    // std::cout << "Dinst::addScr1::Current RAT dinst Inst asm is " << getInst()->get_asm() << std::endl;
    // printf("Dinst::addSrc1::Addsrc_Inst is %ld and isTransient is %b\n", d->getID(), isTransient());
    // std::cout << "Dinst::addScr1::addsrc_dinst Inst asm is " << d->getInst()->get_asm() << std::endl;
    // printf("Dinst::addsrc1:: Before ndeps is: first->getDinst()->ndeps is %d\n", (int)d->getnDeps());

    d->nDeps++;
    // printf("Dinst::addsrc1::ndeps++ is: first->getDinst()->ndeps is %d\n", (int)d->getnDeps());

    I(executed == 0);
    I(d->executed == 0);
    DinstNext* n = &d->pend[0];
    // printf("Dinst::addSrc1:::&d->pend[0]::  is %ld and isTransient is %b\n", n->getDinst()->getID(),
    // n->getDinst()->isTransient()); std::cout << "Dinst::addScr1::&d->pend[0]::  asm is " << n->getDinst()->getInst()->get_asm()
    // << std::endl;
    I(!n->isUsed);
    n->isUsed = true;  // isUsed ==true:: RAW dependence
    n->setParentDinst(this);
    // printf("Dinst::Set parent  Inst is %ld and isTransient is %b for Inst %ld\n", getID(), isTransient(),
    // n->getDinst()->getID());

    I(n->getDinst() == d);
    if (first == 0) {
      first = n;
      // printf("Dinst::addscr1:: setting first is %ld \n", n->getDinst()->getID());
    } else {
      last->nextDep = n;
    }
    n->nextDep = 0;
    last       = n;
  }

  void addSrc2(Dinst* d) {
    I(d->nDeps < MAX_PENDING_SOURCES);

    // printf("Entering Dinst::addSrc2::Current RAT Inst is %ld and isTransient is %b\n", getID(), isTransient());
    // std::cout << "Dinst::addScr2::Current RAT  dinst Inst asm is " << getInst()->get_asm() << std::endl;
    // printf("Dinst::addSrc2::Addsrc2 Inst inst is %ld and isTransient is %b\n", d->getID(), isTransient());
    // std::cout << "Dinst::addScr2::addsrc2 inst asm is " << d->getInst()->get_asm() << std::endl;

    d->nDeps++;
    I(executed == 0);
    I(d->executed == 0);

    // printf("Dinst::addsrc2::  ndeps++ is: first->getDinst()->ndeps is %d\n", (int)d->getnDeps());
    DinstNext* n = &d->pend[1];
    // printf("Dinst::addSrc2::&d->pend[1] ::is %ld and isTransient is %b\n", n->getDinst()->getID(), n->getDinst()->isTransient());
    // std::cout << "Dinst::addScr2::&d->pend[1]::  asm is " << n->getDinst()->getInst()->get_asm() << std::endl;
    I(!n->isUsed);
    n->isUsed = true;  // isUsed ==true: RAW dependence
    n->setParentDinst(this);
    // printf("Dinst::addscr2::Set parent  Inst is %ld and isTransient is %b for Inst %ld\n",
    //        this->getID(),
    //        this->isTransient(),
    //        n->getDinst()->getID());

    I(n->getDinst() == d);
    if (first == 0) {
      first = n;
      // printf("Dinst::first ==0::so setting first = is %ld \n", n->getDinst()->getID());
    } else {
      last->nextDep = n;
      // printf("Dinst::addsrc2::first is %ld \n", first->getDinst()->getID());
    }
    n->nextDep = 0;
    last       = n;
  }

  void addSrc3(Dinst* d) {
    // printf("Dinst::addSrc3::Inst is %ld and isTransient is %b\n", getID(), isTransient());
    // std::cout << "Dinst::addScr3::dinst Inst asm is " << getInst()->get_asm() << std::endl;
    I(d->nDeps < MAX_PENDING_SOURCES);
    d->nDeps++;
    I(executed == 0);
    I(d->executed == 0);

    DinstNext* n = &d->pend[2];
    I(!n->isUsed);
    // printf("Dinst::addSrc3::dinstNextRAW is %ld and isTransient is %b\n", n->getDinst()->getID(), n->getDinst()->isTransient());
    // std::cout << "Dinst::addScr3::dinstNextRAW n asm is " << n->getDinst()->getInst()->get_asm() << std::endl;
    n->isUsed = true;  // isUsed ==true: RAW dependence
    n->setParentDinst(this);

    I(n->getDinst() == d);
    if (first == 0) {
      first = n;
      // printf("Dinst::first is %ld \n", n->getDinst()->getID());
    } else {
      last->nextDep = n;
    }
    n->nextDep = 0;
    last       = n;
  }

  void     setPC(Addr_t a) { pc = a; }
  Addr_t   getPC() const { return pc; }
  Addr_t   getAddr() const { return addr; }
  Hartid_t getFlowId() const { return fid; }

  char getnDeps() const { return nDeps; }
  void decrease_deps() {
    // printf("Dinst::decrease_deps::Now ndeps:: ndeps is:first->ndeps is  %d\n", (int)nDeps);
    nDeps--;
    // printf("Dinst::decrease_deps::Now ndeps--:: ndeps is:first->ndeps-- is  %d\n", (int)nDeps);
  }
  bool isSrc1Ready() const { return !pend[0].isUsed; }  // isUsed ==true ::RAW dependence
  bool isSrc2Ready() const { return !pend[1].isUsed; }
  bool isSrc3Ready() const { return !pend[2].isUsed; }
  void flush_first() { first = nullptr; }
  bool hasPending() const {
    // if (first) {
    //   printf("Dinst::haspending:: Current Inst %ld has pending first ==%ld\n", ID, first->getDinst()->getID());
    // } else {
    //   printf("Dinst::haspending:: Current Inst %ld has pending first== 0 \n", ID);
    // }

    GI(!pend[0].isUsed && !pend[1].isUsed && !pend[2].isUsed, nDeps == 0);
    return first != 0;
  }  // first !=0 means has pending Inst!!!

  bool hasDeps() const {
    //<<<<<<< HEAD
    // if (first) {
    //   printf("Dinst::hasDeps:: Current Inst %ld has pending first %ld\n", ID, first->getDinst()->getID());
    // } else {
    //   printf("Dinst::hasDeps:: Current Inst %ld has pending first== 0 \n", ID);
    // }

    // printf("Dinst::hasdeps current  Inst %ld\n", ID);
    // printf("Dinst::hasdeps::ndeps is %d\n", (int)getnDeps());
    // if (!pend[0].isUsed) {
    //   printf("Dinst::hasdeps:: Pend[0]Inst %ld\n", pend[0].getDinst()->getID());
    // }
    // if (!pend[1].isUsed) {
    //   printf("Dinst:: hasdeps::Pend[1] Inst %ld\n", pend[1].getDinst()->getID());
    // }
    // if (!pend[2].isUsed) {
    //   printf("Dinst:: hasdeps::Pend[2] Inst %ld\n", pend[2].getDinst()->getID());
    // }

    //  if(!isTransient())
    //=======
    if (!isTransient()) {
      //>>>>>>> upstream/main
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

  void markIssuedTransient() {
    // I(issued == 0);
    // I(executing == 0);
    // I(executed == 0);
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
    // I(executed == 0);
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
    // I(executing == 0);
    executing = globalClock;
  }

  bool isReplay() const { return replay; }
  void markReplay() { replay = true; }

  void setBiasBranch(bool b) { biasBranch = b; }
  bool isBiasBranch() const { return biasBranch; }

  void set_zero_delay_taken() { zero_delay_taken = true; }
  void clear_zero_delay_taken() { zero_delay_taken = false; }
  bool is_zero_delay_taken() const { return zero_delay_taken; }

  void setImliHighConf() { imli_highconf = true; }

  bool getImliHighconf() const { return imli_highconf; }

  bool isTaken() const {
    I(getInst()->isControl());
    return addr != 0;
  }

  bool isPerformed() const { return performed; }
  void markPerformed() {
    //<<<<<<< HEAD
    // Loads get performed first, and then executed
    // printf("Dinst ::markPerformed Insit %ld and isTransient is %b\n", getID(), isTransient());

    // printf("Dinst::markPerformed dinst is %ld and isTransient is %b\n", getID(), isTransient());
    // std::cout << "Dinst::markperformed:: inst asm is " << getInst()->get_asm() << std::endl;
    //=======
    //>>>>>>> upstream/main
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
