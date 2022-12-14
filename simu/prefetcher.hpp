// See LICENSE for details.

#pragma once

#include "addresspredictor.hpp"
#include "cachecore.hpp"
#include "callback.hpp"
#include "port.hpp"
#include "stats.hpp"

class MemObj;

class Prefetcher {
private:
  MemObj *DL1;  // L1 cache

  Stats_avg  avgPrefetchNum;
  Stats_avg  avgPrefetchConf;
  Stats_hist histPrefetchDelta;

  std::unique_ptr<AddressPredictor> apred;

  int32_t degree;
  int32_t distance;

  int32_t  curPrefetch;
  uint32_t lineSizeBits;

  Addr_t pref_sign;

  bool         pending_prefetch;
  Addr_t       pending_preq_pc;
  uint16_t     pending_preq_conf;
  bool         pending_statsFlag;
  FetchEngine *pending_chain_fetch;

  uint16_t conf = 0;
  Addr_t   pending_preq_addr;

  void nextPrefetch();

  StaticCallbackMember0<Prefetcher, &Prefetcher::nextPrefetch> nextPrefetchCB;

public:
  Prefetcher(MemObj *l1, int cpud_id);
  ~Prefetcher() {}

  void exe(Dinst *dinst);
  void ret(Dinst *dinst);
};
