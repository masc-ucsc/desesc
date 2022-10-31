// See LICENSE for details.

#pragma once

#include "callback.hpp"

#include "estl.h"
#include "GStats.h"
#include "Port.h"
#include "AddressPredictor.h"
#include "CacheCore.h"

class MemObj;

class Prefetcher {
private:
  GStatsAvg  avgPrefetchNum;
  GStatsAvg  avgPrefetchConf;
  GStatsHist histPrefetchDelta;

  AddressPredictor *apred;

  int32_t  maxPrefetch;
  int32_t  minDistance;
  int32_t  pfStride;
  int32_t  curPrefetch;
  uint32_t lineSizeBits;

  AddrType pref_sign;

  bool         pending_prefetch;
  AddrType     pending_preq_pc;
  uint16_t     pending_preq_conf;
  bool         pending_statsFlag;
  FetchEngine *pending_chain_fetch;

  uint16_t conf = 0;
  AddrType pending_preq_addr;

  void nextPrefetch();

  MemObj *DL1; // L1 cache

  StaticCallbackMember0<Prefetcher, &Prefetcher::nextPrefetch> nextPrefetchCB;

public:
  Prefetcher(MemObj *l1, int cpud_id);
  ~Prefetcher() {
  }

  void exe(Dinst *dinst);
  void ret(Dinst *dinst);
};

