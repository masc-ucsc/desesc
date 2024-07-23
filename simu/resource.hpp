// See LICENSE for details.

#pragma once

#include "bloomfilter.hpp"
#include "callback.hpp"
#include "fastqueue.hpp"
#include "gmemory_system.hpp"
#include "iassert.hpp"
#include "prefetcher.hpp"
#include "stats.hpp"
#include "store_buffer.hpp"
#include "storeset.hpp"

class PortGeneric;
class Dinst;
class MemObj;
class Cluster;

enum StallCause {
  NoStall = 0,
  SmallWinStall,
  SmallROBStall,
  SmallREGStall,
  DivergeStall,
  OutsLoadsStall,
  OutsStoresStall,
  OutsBranchesStall,
  ReplaysStall,
  SyscallStall,
  MaxStall,
  Suspend
};

class LSQ;

class Resource {
protected:
  const std::shared_ptr<Cluster> cluster;
  PortGeneric *const             gen;

  Stats_avg  avgRenameTime;
  Stats_avg  avgIssueTime;
  Stats_avg  avgExecuteTime;
  Stats_avg  avgRetireTime;
  Stats_hist safeHitTimeHist;
  Stats_hist specHitTimeHist;
  Stats_hist latencyHitTimeHist;

  const TimeDelta_t lat;
  const int32_t     coreid;

  Time_t usedTime;
  bool   inorder;

  Resource(Opcode type, std::shared_ptr<Cluster> cls, PortGeneric *gen, TimeDelta_t l, uint32_t cpuid);

  void setStats(const Dinst *dinst);

public:
  Resource(const Resource &)            = delete;
  Resource &operator=(const Resource &) = delete;
  Resource(Resource &&)                 = delete;
  Resource &operator=(Resource &&)      = delete;
  virtual ~Resource();

  [[nodiscard]] const std::shared_ptr<Cluster> getCluster() const { return cluster; }
  std::shared_ptr<Cluster>                     getCluster() { return cluster; }

  // Sequence:
  //
  // 1st) A canIssue check is done with "canIssue". This checks that the cluster
  // can accept another request (cluster window size), and that additional
  // structures (like the LD/ST queue entry) also have enough resources.
  //
  // 2nd) The timing to calculate when the inputs are ready is done at
  // executing.
  //
  // 3rd) executed is called the instructions has been executed. It may be
  // called through Dinst::doAtExecuted
  //
  // 4th) When the instruction is retired from the ROB retire is called

  virtual StallCause canIssue(Dinst *dinst)                 = 0;
  virtual void       executing(Dinst *dinst)                = 0;
  virtual void       executed(Dinst *dinst)                 = 0;
  virtual bool       preretire(Dinst *dinst, bool flushing) = 0;
  virtual bool       retire(Dinst *dinst, bool flushing)    = 0;
  virtual void       performed(Dinst *dinst)                = 0;
  virtual bool       flushed(Dinst *dinst)                  = 0;
  virtual bool       try_flushed(Dinst *dinst)              = 0;

  using executingCB = CallbackMember1<Resource, Dinst *, &Resource::executing>;
  using executedCB  = CallbackMember1<Resource, Dinst *, &Resource::executed>;
  using performedCB = CallbackMember1<Resource, Dinst *, &Resource::performed>;

  [[nodiscard]] Time_t getUsedTime() const { return usedTime; }
  void                 setUsedTime() { usedTime = globalClock; }
};

class MemReplay : public Resource {
protected:
  const uint32_t lfSize;

  std::shared_ptr<StoreSet> storeset;
  void                      replayManage(Dinst *dinst);
  struct FailType {
    FailType() { ssid = -1; }
    SSID_t ssid;
    Time_t id;
    Addr_t pc;
    Addr_t addr;
    Addr_t data;
    Opcode op;
  };
  std::vector<FailType> lf;

public:
  MemReplay(Opcode type, std::shared_ptr<Cluster> cls, PortGeneric *gen, std::shared_ptr<StoreSet> ss, TimeDelta_t l,
            uint32_t cpuid);
};

class MemResource : public MemReplay {
private:
protected:
  MemObj                       *firstLevelMemObj;
  MemObj                       *DL1;
  LSQ                          *lsq;
  std::shared_ptr<Prefetcher>   pref;
  std::shared_ptr<Store_buffer> scb;
  Stats_cntr                    stldViolations;

  bool LSQlateAlloc;

  MemResource(Opcode type, std::shared_ptr<Cluster> cls, PortGeneric *aGen, LSQ *lsq, std::shared_ptr<StoreSet> ss,
              std::shared_ptr<Prefetcher> pref, std::shared_ptr<Store_buffer> scb, TimeDelta_t l,
              std::shared_ptr<Gmemory_system> ms, int32_t id, const std::string &cad);

public:
};

class FULoad : public MemResource {
private:
  const TimeDelta_t LSDelay;

  int32_t freeEntries;
  bool    enableDcache;
#ifdef MEM_TSO2
  Stats_cntr tso2Replay;
#endif

protected:
  void cacheDispatched(Dinst *dinst);
  using cacheDispatchedCB = CallbackMember1<FULoad, Dinst *, &FULoad::cacheDispatched>;

public:
  FULoad(Opcode type, std::shared_ptr<Cluster> cls, PortGeneric *aGen, LSQ *lsq, std::shared_ptr<StoreSet> ss,
         std::shared_ptr<Prefetcher> pref, std::shared_ptr<Store_buffer> scb, TimeDelta_t lsdelay, TimeDelta_t l,
         std::shared_ptr<Gmemory_system> ms, int32_t size, int32_t id, const std::string &cad);

  StallCause canIssue(Dinst *dinst)                 final;
  void       executing(Dinst *dinst)                final;
  void       executed(Dinst *dinst)                 final;
  bool       preretire(Dinst *dinst, bool flushing) final;
  bool       retire(Dinst *dinst, bool flushing)    final;
  void       performed(Dinst *dinst)                final;
  bool       flushed(Dinst *dinst)                  final;
  bool       try_flushed(Dinst *dinst)              final;
};

class FUStore : public MemResource {
private:
  int32_t freeEntries;
  bool    enableDcache;

public:
  FUStore(Opcode type, std::shared_ptr<Cluster> cls, PortGeneric *aGen, LSQ *lsq, std::shared_ptr<StoreSet> ss,
          std::shared_ptr<Prefetcher> pref, std::shared_ptr<Store_buffer> scb, TimeDelta_t l, std::shared_ptr<Gmemory_system> ms,
          int32_t size, int32_t id, const std::string &cad);

  StallCause canIssue(Dinst *dinst)                 final;
  void       executing(Dinst *dinst)                final;
  void       executed(Dinst *dinst)                 final;
  bool       preretire(Dinst *dinst, bool flushing) final;
  bool       retire(Dinst *dinst, bool flushing)    final;
  void       performed(Dinst *dinst)                final;
  bool       flushed(Dinst *dinst)                  final;
  bool       try_flushed(Dinst *dinst)              final;
};

class FUGeneric : public Resource {
private:
protected:
public:
  FUGeneric(Opcode type, std::shared_ptr<Cluster> cls, PortGeneric *aGen, TimeDelta_t l, uint32_t cpuid);

  StallCause canIssue(Dinst *dinst)                 final;
  void       executing(Dinst *dinst)                final;
  void       executed(Dinst *dinst)                 final;
  bool       preretire(Dinst *dinst, bool flushing) final;
  bool       retire(Dinst *dinst, bool flushing)    final;
  void       performed(Dinst *dinst)                final;
  bool       flushed(Dinst *dinst)                  final;
  bool       try_flushed(Dinst *dinst)              final;
};

class FUBranch : public Resource {
private:
  int32_t freeBranches;
  bool    drainOnMiss;

protected:
public:
  FUBranch(Opcode type, std::shared_ptr<Cluster> cls, PortGeneric *aGen, TimeDelta_t l, uint32_t cpuid, int32_t mb, bool dom);

  StallCause canIssue(Dinst *dinst)                 final;
  void       executing(Dinst *dinst)                final;
  void       executed(Dinst *dinst)                 final;
  bool       preretire(Dinst *dinst, bool flushing) final;
  bool       retire(Dinst *dinst, bool flushing)    final;
  void       performed(Dinst *dinst)                final;
  bool       flushed(Dinst *dinst)                  final;
  bool       try_flushed(Dinst *dinst)              final;
};

class FURALU : public Resource {
private:
  Stats_cntr dmemoryBarrier;
  Stats_cntr imemoryBarrier;
  Time_t     blockUntil;

protected:
public:
  FURALU(Opcode type, std::shared_ptr<Cluster> cls, PortGeneric *aGen, TimeDelta_t l, int32_t id);

  StallCause canIssue(Dinst *dinst)                 final;
  void       executing(Dinst *dinst)                final;
  void       executed(Dinst *dinst)                 final;
  bool       preretire(Dinst *dinst, bool flushing) final;
  bool       retire(Dinst *dinst, bool flushing)    final;
  void       performed(Dinst *dinst)                final;
  bool       flushed(Dinst *dinst)                  final;
  bool       try_flushed(Dinst *dinst)              final;
};
