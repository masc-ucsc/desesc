// See LICENSE for details.

#pragma once

// #define WAVESNAP_EN

#include <stdint.h>

// Generic Processor Interface.
//
// This class is a generic interface for Processors. It has been
// design for Traditional and SMT processors in mind. That's the
// reason why it manages the execution engine (RDEX).

#include "callback.hpp"
#include "cluster.hpp"
#include "clustermanager.hpp"
#include "estl.hpp"
#include "fastqueue.hpp"
#include "gmemory_system.hpp"
#include "iassert.hpp"
#include "instruction.hpp"
#include "lsq.hpp"
#include "pipeline.hpp"
#include "prefetcher.hpp"
#include "resource.hpp"
#include "simu_base.hpp"
#include "snippets.hpp"
#include "stats.hpp"
#include "store_buffer.hpp"
#include "wavesnap.hpp"

#include <random>

class BPredictor;

struct SMT_fetch {
  std::vector<std::shared_ptr<FetchEngine>> fe;
  Time_t                                    smt_lastTime;
  int                                       smt_cnt;
  int                                       smt_active;
  int                                       smt_turn;

  SMT_fetch() {
    smt_lastTime = 0;
    smt_cnt      = 1;
    smt_active   = 1;
    smt_turn     = 0;
  };

  std::shared_ptr<FetchEngine> fetch_next();
  void                         update();
};

class GProcessor : public Simu_base {
private:
protected:
  const int32_t FetchWidth;
  const int32_t IssueWidth;
  const int32_t RetireWidth;
  const int32_t RealisticWidth;
  const int32_t InstQueueSize;
  const size_t  MaxROBSize;

  size_t                          smt_size;
  std::shared_ptr<Gmemory_system> memorySystem;

  std::shared_ptr<StoreSet>     storeset;
  std::shared_ptr<Prefetcher>   prefetcher;
  std::shared_ptr<Store_buffer> scb;

  FastQueue<Dinst *> rROB;  // ready/retiring/executed ROB
  FastQueue<Dinst *> ROB;

  uint32_t smt;  // 1...
  bool     busy;

  // BEGIN  Statistics
  Stats_avg                                         avgFetchWidth;
  std::array<std::unique_ptr<Stats_cntr>, MaxStall> nStall;
  Opcode_array<std::unique_ptr<Stats_cntr>>         nInst;

  // OoO Stats
  Stats_avg  rrobUsed;
  Stats_avg  robUsed;
  Stats_avg  nReplayInst;
  Stats_cntr nCommitted;  // committed instructions

  // "Lack of Retirement" Stats
  Stats_cntr noFetch;
  Stats_cntr noFetch2;

  // END Statistics

  uint64_t lastReplay;

  // Construction
  void buildInstStats(const std::string &txt);
  void buildUnit(const std::string &clusterName, std::shared_ptr<Gmemory_system> ms, Cluster *cluster, Opcode type);

  GProcessor(std::shared_ptr<Gmemory_system> gm, Hartid_t i);

  int32_t issue();
  void    fetch();

  virtual StallCause add_inst(Dinst *dinst) = 0;

  bool use_stats;  // Stats mode to use when dinst->has_stats() is not available

  static inline absl::flat_hash_map<std::string, std::shared_ptr<SMT_fetch>> fetch_map;

  SMT_fetch smt_fetch;

  PipeQueue pipeQ;
  int32_t   spaceInInstQueue;

  bool decode_stage();

public:
#ifdef WAVESNAP_EN
  std::unique_ptr<Wavesnap> snap;
#endif
  virtual ~GProcessor();

  virtual void   executing(Dinst *dinst) = 0;
  virtual void   executed(Dinst *dinst)  = 0;
  virtual void   flushed(Dinst *dinst)  = 0;
  virtual LSQ   *getLSQ()                = 0;
  virtual bool   is_nuking()             = 0;
  virtual bool   isReplayRecovering()    = 0;
  virtual Time_t getReplayID()           = 0;

  virtual void replay(Dinst *target) { (void)target; };  // = 0;

  bool isROBEmpty() const { return (ROB.empty() && rROB.empty()); }
  int  getROBsize() const { return (ROB.size() + rROB.size()); }
  bool isROBEmptyOnly() const { return ROB.empty(); }

  int getROBSizeOnly() const { return ROB.size(); }

  uint32_t getIDFromTop(int position) const { return ROB.getIDFromTop(position); }
  Dinst   *getData(uint32_t position) const { return ROB.getData(position); }

  // Returns the maximum number of flows this processor can support
  size_t get_smt_size() const override { return smt_size; }

  void add_inst_transient_on_branch_miss(IBucket *bucket, Addr_t pc);
  void flush_transient_inst_on_fetch_ready();
  void flush_transient_inst_from_inst_queue();
  void flush_transient_from_rob();
  void report(const std::string &str);
  
  Addr_t random_addr_Gen();
  
  std::shared_ptr<StoreSet>     ref_SS() { return storeset; }
  std::shared_ptr<Prefetcher>   ref_prefetcher() { return prefetcher; }
  std::shared_ptr<Store_buffer> ref_SCB() { return scb; }
  friend class Cluster;
};
