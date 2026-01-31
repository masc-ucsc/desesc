// See LICENSE for details.

#pragma once

#include "addresspredictor.hpp"
#include "bpred.hpp"
#include "emul_base.hpp"
#include "gmemory_system.hpp"
#include "iassert.hpp"
#include "stats.hpp"

class IBucket;
class GProcessor;
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
                  // #ifndef NDEBUG
  Dinst* missDinst;
  // #endif

  // Dinst *transientDinst;

  CallbackContainer cbPending;

  Time_t lastFetchBubbleTime;

  bool il1_enable;

  bool processBranch(Dinst* dinst);

  // ******************* Statistics section
  Stats_avg  avgEntryFetchLost;
  Stats_hist avgFastFixWasteTime;
  Stats_hist avgSlowFixWasteTime;
  Stats_avg  avgSlowFixWasteInst;
  Stats_avg  avgFastFixWasteInst;
  Stats_hist avgFetchTime;
  Stats_avg  avgBucketInst;
  Stats_avg  avgBeyondFBInst;
  Stats_avg  avgFetchOneLineWasteInst;
  Stats_avg  avgFetchStallInst;
  Stats_avg  avgBB;
  Stats_avg  avgFB;
  // *******************

public:
  FetchEngine(Hartid_t i, std::shared_ptr<Gmemory_system> gms, std::shared_ptr<BPredictor> shared_bpred = nullptr);

  ~FetchEngine();

  void fetch(IBucket* buffer, std::shared_ptr<Emul_base> eint, Hartid_t fid, GProcessor* gproc);

  typedef CallbackMember4<FetchEngine, IBucket*, std::shared_ptr<Emul_base>, Hartid_t, GProcessor*, &FetchEngine::fetch> fetchCB;

  void realfetch(IBucket* buffer, std::shared_ptr<Emul_base> eint, Hartid_t fid, int32_t n2Fetched, GProcessor* gproc);

  void chainPrefDone(Addr_t pc, int distance, Addr_t addr);
  void chainLoadDone(Dinst* dinst);
  typedef CallbackMember3<FetchEngine, Addr_t, int, Addr_t, &FetchEngine::chainPrefDone> chainPrefDoneCB;

  void                                                                             unBlockFetch(Dinst* dinst, Time_t missFetchTime);
  typedef CallbackMember2<FetchEngine, Dinst*, Time_t, &FetchEngine::unBlockFetch> unBlockFetchCB;

  void unBlockFetchBPredDelay(Dinst* dinst, Time_t missFetchTime);
  typedef CallbackMember2<FetchEngine, Dinst*, Time_t, &FetchEngine::unBlockFetchBPredDelay> unBlockFetchBPredDelayCB;

  void dump(const std::string& str) const;

  Dinst* transientDinst;
  bool   isBlocked() const { return missInst; }
  // #ifndef NDEBUG
  Dinst* getMissDinst() const { return missDinst; }
  // #endif

  Dinst* get_next_transient_dinst() const {
    I(0);
    return transientDinst + 4;  // BAD BAD CODE!!!. It should get a new dinst with PC+4 not pointer+4
  }

  Dinst* get_miss_dinst() const { return transientDinst; }

  bool is_fetch_next_ready;
  bool get_is_fetch_next_ready() { return is_fetch_next_ready; }
  void reset_is_fetch_next_ready() { is_fetch_next_ready = false; }

  void clearMissInst(Dinst* dinst, Time_t missFetchTime);

  std::shared_ptr<BPredictor> ref_bpred() { return bpred; }
};
