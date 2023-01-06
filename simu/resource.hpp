// See LICENSE for details.

#pragma once

#include "bloomfilter.hpp"
#include "fastqueue.hpp"
#include "prefetcher.hpp"
#include "storeset.hpp"
#include "callback.hpp"
#include "iassert.hpp"
#include "stats.hpp"
#include "store_buffer.hpp"
#include "gmemorysystem.hpp"

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
  PortGeneric *const gen;

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

  Resource(uint8_t type, std::shared_ptr<Cluster> cls, PortGeneric *gen, TimeDelta_t l, uint32_t cpuid);

  void setStats(const Dinst *dinst);

public:
  virtual ~Resource();

  const std::shared_ptr<Cluster> getCluster() const { return cluster; }
  std::shared_ptr<Cluster>       getCluster() { return cluster; }

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

  typedef CallbackMember1<Resource, Dinst *, &Resource::executing> executingCB;
  typedef CallbackMember1<Resource, Dinst *, &Resource::executed>  executedCB;
  typedef CallbackMember1<Resource, Dinst *, &Resource::performed> performedCB;

  Time_t getUsedTime() const { return usedTime; }
  void   setUsedTime() { usedTime = globalClock; }
};

class MemReplay : public Resource {
protected:
  const uint32_t lfSize;

  std::shared_ptr<StoreSet> storeset;
  void                      replayManage(Dinst *dinst);
  struct FailType {
    FailType() {
      ssid = -1;
    }
    SSID_t ssid;
    Time_t id;
    Addr_t pc;
    Addr_t addr;
    Addr_t data;
    Opcode op;
  };
  std::vector<FailType> lf;

public:
  MemReplay(uint8_t type, std::shared_ptr<Cluster> cls, PortGeneric *gen, std::shared_ptr<StoreSet> ss, TimeDelta_t l, uint32_t cpuid);
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

  MemResource(uint8_t type, std::shared_ptr<Cluster> cls, PortGeneric *aGen, LSQ *lsq, std::shared_ptr<StoreSet> ss,
              std::shared_ptr<Prefetcher> pref, std::shared_ptr<Store_buffer> scb, TimeDelta_t l, std::shared_ptr<GMemorySystem> ms, int32_t id,
              const char *cad);

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
  void                                                               cacheDispatched(Dinst *dinst);
  typedef CallbackMember1<FULoad, Dinst *, &FULoad::cacheDispatched> cacheDispatchedCB;

public:
  FULoad(uint8_t type, std::shared_ptr<Cluster> cls, PortGeneric *aGen, LSQ *lsq, std::shared_ptr<StoreSet> ss, std::shared_ptr<Prefetcher> pref,
         std::shared_ptr<Store_buffer> scb, TimeDelta_t lsdelay, TimeDelta_t l, std::shared_ptr<GMemorySystem> ms, int32_t size, int32_t id,
         const char *cad);

  StallCause canIssue(Dinst *dinst);
  void       executing(Dinst *dinst);
  void       executed(Dinst *dinst);
  bool       preretire(Dinst *dinst, bool flushing);
  bool       retire(Dinst *dinst, bool flushing);
  void       performed(Dinst *dinst);
};

class FUStore : public MemResource {
private:
  int32_t freeEntries;
  bool    enableDcache;

public:
  FUStore(uint8_t type, std::shared_ptr<Cluster> cls, PortGeneric *aGen, LSQ *lsq, std::shared_ptr<StoreSet> ss, std::shared_ptr<Prefetcher> pref,
          std::shared_ptr<Store_buffer> scb, TimeDelta_t l, std::shared_ptr<GMemorySystem> ms, int32_t size, int32_t id, const char *cad);

  StallCause canIssue(Dinst *dinst);
  void       executing(Dinst *dinst);
  void       executed(Dinst *dinst);
  bool       preretire(Dinst *dinst, bool flushing);
  bool       retire(Dinst *dinst, bool flushing);
  void       performed(Dinst *dinst);
};

class FUGeneric : public Resource {
private:
protected:
public:
  FUGeneric(uint8_t type, std::shared_ptr<Cluster> cls, PortGeneric *aGen, TimeDelta_t l, uint32_t cpuid);

  StallCause canIssue(Dinst *dinst);
  void       executing(Dinst *dinst);
  void       executed(Dinst *dinst);
  bool       preretire(Dinst *dinst, bool flushing);
  bool       retire(Dinst *dinst, bool flushing);
  void       performed(Dinst *dinst);
};

class FUBranch : public Resource {
private:
  int32_t freeBranches;
  bool    drainOnMiss;

protected:
public:
  FUBranch(uint8_t type, std::shared_ptr<Cluster> cls, PortGeneric *aGen, TimeDelta_t l, uint32_t cpuid, int32_t mb, bool dom);

  StallCause canIssue(Dinst *dinst);
  void       executing(Dinst *dinst);
  void       executed(Dinst *dinst);
  bool       preretire(Dinst *dinst, bool flushing);
  bool       retire(Dinst *dinst, bool flushing);
  void       performed(Dinst *dinst);
};

class FURALU : public Resource {
private:
  Stats_cntr dmemoryBarrier;
  Stats_cntr imemoryBarrier;
  Time_t     blockUntil;

protected:
public:
  FURALU(uint8_t type, std::shared_ptr<Cluster> cls, PortGeneric *aGen, TimeDelta_t l, int32_t id);

  StallCause canIssue(Dinst *dinst);
  void       executing(Dinst *dinst);
  void       executed(Dinst *dinst);
  bool       preretire(Dinst *dinst, bool flushing);
  bool       retire(Dinst *dinst, bool flushing);
  void       performed(Dinst *dinst);
};
