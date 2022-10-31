// See LICENSE for details.

#pragma once

#include <vector>

#include "dinst.hpp"
#include "callback.hpp"

#include "estl.h"
#include "GStats.h"

#define STORESET_MERGING 1
#define STORESET_CLEARING 1
#define CLR_INTRVL 90000000

class StoreSet {
private:
  typedef std::vector<SSID_t>  SSIT_t;
  typedef std::vector<DInst *> LFST_t;

  SSIT_t SSIT;
  LFST_t LFST;

  SSID_t StoreSetSize;

#ifdef STORESET_CLEARING
  Time_t                                                          clear_interval;
  void                                                            clearStoreSetsTimer();
  StaticCallbackMember0<StoreSet, &StoreSet::clearStoreSetsTimer> clearStoreSetsTimerCB;
#endif

  AddrType hashPC(AddrType PC) const {
    //    return ((PC>>2) % 8191);
    return ((((PC >> 2) ^ (PC >> 11)) + PC) >> 2) & (StoreSetSize - 1);
  }

  bool isValidSSID(SSID_t SSID) const {
    return SSID != -1;
  }

  // SSIT Functions
  SSID_t get_SSID(AddrType PC) const {
    return SSIT[hashPC(PC)];
  };
  void clear_SSIT();

  // LFST Functions
  DInst *get_LFS(SSID_t SSID) const {
    I(SSID <= (int32_t)LFST.size());
    return LFST[SSID];
  };
  void set_LFS(SSID_t SSID, DInst *dinst) {
    LFST[SSID] = dinst;
  }
  void clear_LFST(void);

  SSID_t create_id();
  void   set_SSID(AddrType PC, SSID_t SSID) {
    SSIT[hashPC(PC)] = SSID;
  };
  SSID_t create_set(AddrType);

#if 1
  // TO - delete
  void stldViolation_withmerge(DInst *ld_dinst, DInst *st_dinst);
  void VPC_misspredict(DInst *ld_dinst, AddrType store_pc);
  void assign_SSID(DInst *dinst, SSID_t SSID);
#endif
public:
  StoreSet(const int32_t cpu_id);
  ~StoreSet() {
  }

  bool insert(DInst *dinst);
  void remove(DInst *dinst);
  void stldViolation(DInst *ld_dinst, AddrType st_pc);
  void stldViolation(DInst *ld_dinst, DInst *st_dinst);

  SSID_t mergeset(SSID_t id1, SSID_t id2);

#ifdef STORESET_MERGING
  // move violating load to qdinst load's store set, stores will migrate as violations occur.
  void merge_sets(DInst *m_dinst, DInst *d_dinst);
#else
  void merge_sets(DInst *m_dinst, DInst *d_dinst){};
#endif
};

