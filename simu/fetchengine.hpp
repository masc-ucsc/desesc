// See LICENSE for details.

#pragma once

#include "addresspredictor.hpp"
#include "bpred.hpp"
#include "emul_base.hpp"
#include "gmemory_system.hpp"
#include "iassert.hpp"
#include "stats.hpp"

// #define ENABLE_LDBP

class IBucket;

class FetchEngine {
private:
  std::shared_ptr<Gmemory_system> const gms;

  std::shared_ptr<BPredictor> bpred;

  uint16_t fetch_width;
  uint16_t half_fetch_width;

  bool fetch_one_line;
  bool fetch_align;
  bool trace_align;

  uint16_t max_bb_cycle;
  uint16_t maxBB;

  TimeDelta_t il1_hit_delay;
  uint16_t    il1_line_size;
  uint16_t    il1_line_bits;

  // InstID of the address that generated a misprediction

  bool missInst;  // branch missprediction. Stop fetching until solved
#ifndef NDEBUG
  Dinst *missDinst;
#endif
  
  //Dinst *transientDinst;

  CallbackContainer cbPending;

  Time_t lastMissTime;  // FIXME: maybe we need an array

  bool il1_enable;

protected:
  // bool processBranch(Dinst *dinst, uint16_t n2Fetched);
  bool processBranch(Dinst *dinst, uint16_t n2Fetchedi);

  // ******************* Statistics section
  Stats_avg  avgFetchLost;
  Stats_avg  avgBranchTime;
  Stats_avg  avgBranchTime2;
  Stats_avg  avgFetchTime;
  Stats_avg  avgFetched;
  Stats_cntr nDelayInst1;
  Stats_cntr nDelayInst2;
  Stats_cntr nDelayInst3;
  Stats_cntr nBTAC;
  Stats_cntr zeroDinst;
#ifdef ESESC_TRACE_DATA
  Stats_hist dataHist;
  Stats_hist dataSignHist;
  Stats_hist nbranchMissHist;
  Stats_hist nLoadData_per_branch;
  Stats_hist nLoadAddr_per_branch;
#endif
  // *******************

public:
  FetchEngine(Hartid_t i, std::shared_ptr<Gmemory_system> gms, std::shared_ptr<BPredictor> shared_bpred = nullptr);

  ~FetchEngine();

#ifdef ENABLE_LDBP

  Dinst  *init_ldbp(Dinst *dinst, Data_t dd, Addr_t ldpc);
  MemObj *DL1;
  Dinst  *ld_dinst;
  Addr_t  dep_pc;  // dependent instn's PC
  int     fetch_br_count;

  Addr_t   pref_addr;
  Addr_t   check_line_addr;
  Addr_t   base_addr;
  Addr_t   tmp_base_addr;
  uint64_t p_delta;
  uint64_t inf;
  uint64_t constant;
#endif

  void fetch(IBucket *buffer, std::shared_ptr<Emul_base> eint, Hartid_t fid);

  typedef CallbackMember3<FetchEngine, IBucket *, std::shared_ptr<Emul_base>, Hartid_t, &FetchEngine::fetch> fetchCB;

  void realfetch(IBucket *buffer, std::shared_ptr<Emul_base> eint, Hartid_t fid, int32_t n2Fetched);

  void chainPrefDone(Addr_t pc, int distance, Addr_t addr);
  void chainLoadDone(Dinst *dinst);
  typedef CallbackMember3<FetchEngine, Addr_t, int, Addr_t, &FetchEngine::chainPrefDone> chainPrefDoneCB;

  void unBlockFetch(Dinst *dinst, Time_t missFetchTime);
  typedef CallbackMember2<FetchEngine, Dinst *, Time_t, &FetchEngine::unBlockFetch> unBlockFetchCB;

  void unBlockFetchBPredDelay(Dinst *dinst, Time_t missFetchTime);
  typedef CallbackMember2<FetchEngine, Dinst *, Time_t, &FetchEngine::unBlockFetchBPredDelay> unBlockFetchBPredDelayCB;

#if 0
  void unBlockFetch();
  StaticCallbackMember0<FetchEngine,&FetchEngine::unBlockFetch> unBlockFetchCB;

  void unBlockFetchBPredDelay();
  StaticCallbackMember0<FetchEngine,&FetchEngine::unBlockFetchBPredDelay> unBlockFetchBPredDelayCB;
#endif

  void dump(const std::string &str) const;

  Dinst *transientDinst;
  bool isBlocked() const { return missInst; }
#ifndef NDEBUG
  Dinst *getMissDinst() const { return missDinst; }
#endif

  Dinst *get_next_transient_dinst() const { return transientDinst; }

  Dinst *get_miss_dinst() const { return transientDinst; }
  void setTransientInst(Dinst *dinst);

  void clearMissInst(Dinst *dinst, Time_t missFetchTime);
  void setMissInst(Dinst *dinst);

  std::shared_ptr<BPredictor> ref_bpred() { return bpred; }
};
