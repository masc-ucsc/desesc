// See LICENSE for details.

#pragma once

#include <vector>

#include "callback.hpp"
#include "dinst.hpp"
#include "estl.h"

#define STORESET_MERGING  1
#define STORESET_CLEARING 1
#define CLR_INTRVL        90000000

class StoreSet {
private:
  typedef std::vector<SSID_t>  SSIT_t;
  typedef std::vector<Dinst *> LFST_t;

  SSIT_t SSIT;
  LFST_t LFST;

  SSID_t StoreSetSize;

#ifdef STORESET_CLEARING
  Time_t                                                          clear_interval;
  void                                                            clearStoreSetsTimer();
  StaticCallbackMember0<StoreSet, &StoreSet::clearStoreSetsTimer> clearStoreSetsTimerCB;
#endif

  uint64_t hashPC(uint64_t PC) const {
    //    return ((PC>>2) % 8191);
    return ((((PC >> 2) ^ (PC >> 11)) + PC) >> 2) & (StoreSetSize - 1);
  }

  bool isValidSSID(SSID_t SSID) const { return SSID != -1; }

  // SSIT Functions
  SSID_t get_SSID(uint64_t PC) const { return SSIT[hashPC(PC)]; };
  void   clear_SSIT();

  // LFST Functions
  Dinst *get_LFS(SSID_t SSID) const {
    I(SSID <= (int32_t)LFST.size());
    return LFST[SSID];
  };
  void set_LFS(SSID_t SSID, Dinst *dinst) { LFST[SSID] = dinst; }
  void clear_LFST(void);

  SSID_t create_id();
  void   set_SSID(uint64_t PC, SSID_t SSID) { SSIT[hashPC(PC)] = SSID; };
  SSID_t create_set(uint64_t);

#if 1
  // TO - delete
  void stldViolation_withmerge(Dinst *ld_dinst, Dinst *st_dinst);
  void VPC_misspredict(Dinst *ld_dinst, uint64_t store_pc);
  void assign_SSID(Dinst *dinst, SSID_t SSID);
#endif
public:
  StoreSet(const int32_t hid);
  ~StoreSet();

  bool insert(Dinst *dinst);
  void remove(Dinst *dinst);
  void stldViolation(Dinst *ld_dinst, uint64_t st_pc);
  void stldViolation(Dinst *ld_dinst, Dinst *st_dinst);

  SSID_t mergeset(SSID_t id1, SSID_t id2);

#ifdef STORESET_MERGING
  // move violating load to qdinst load's store set, stores will migrate as violations occur.
  void merge_sets(Dinst *m_dinst, Dinst *d_dinst);
#else
  void merge_sets(Dinst *m_dinst, Dinst *d_dinst){};
#endif
};
