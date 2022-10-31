// See LICENSE for details.

#pragma once

#include "callback.hpp"
#include "iassert.hpp"

#include "GStats.h"
#include "BloomFilter.h"
#include "FastQueue.h"
#include "SCB.h"
#include "Prefetcher.h"
#include "StoreSet.h"

class PortGeneric;
class DInst;
class MemObj;
class Cluster;
class GProcessor;

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
  Cluster *const     cluster;
  PortGeneric *const gen;

  GStatsAvg avgRenameTime;
  GStatsAvg avgIssueTime;
  GStatsAvg avgExecuteTime;
  GStatsAvg avgRetireTime;
  GStatsHist safeHitTimeHist;
  GStatsHist specHitTimeHist;
  GStatsHist latencyHitTimeHist;

  const TimeDelta_t lat;
  const int32_t     coreid;

  Time_t usedTime;
  bool   inorder;

  Resource(uint8_t type, Cluster *cls, PortGeneric *gen, TimeDelta_t l, uint32_t cpuid);

  void setStats(const DInst *dinst);

public:
  virtual ~Resource();

  const Cluster *getCluster() const {
    return cluster;
  }
  Cluster *getCluster() {
    return cluster;
  }

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
  // called through DInst::doAtExecuted
  //
  // 4th) When the instruction is retired from the ROB retire is called

  virtual StallCause canIssue(DInst *dinst)                 = 0;
  virtual void       executing(DInst *dinst)                = 0;
  virtual void       executed(DInst *dinst)                 = 0;
  virtual bool       preretire(DInst *dinst, bool flushing) = 0;
  virtual bool       retire(DInst *dinst, bool flushing)    = 0;
  virtual void       performed(DInst *dinst)                = 0;

  typedef CallbackMember1<Resource, DInst *, &Resource::executing> executingCB;
  typedef CallbackMember1<Resource, DInst *, &Resource::executed>  executedCB;
  typedef CallbackMember1<Resource, DInst *, &Resource::performed> performedCB;

  Time_t getUsedTime() const {
    return usedTime;
  }
  void setUsedTime() {
    usedTime = globalClock;
  }
};

class GMemorySystem;

class MemReplay : public Resource {
protected:
  const uint32_t lfSize;

  StoreSet *storeset;
  void      replayManage(DInst *dinst);
  struct FailType {
    SSID_t     ssid;
    Time_t     id;
    AddrType   pc;
    AddrType   addr;
    AddrType   data;
    InstOpcode op;
  };
  FailType *lf;

public:
  MemReplay(uint8_t type, Cluster *cls, PortGeneric *gen, StoreSet *ss, TimeDelta_t l, uint32_t cpuid);
};

class MemResource : public MemReplay {
private:
protected:
  MemObj *            firstLevelMemObj;
  MemObj *            DL1;
  GMemorySystem *     memorySystem;
  LSQ *               lsq;
  Prefetcher *        pref;
  SCB*                scb;
  GStatsCntr          stldViolations;
  
  bool                LSQlateAlloc;

  MemResource(uint8_t type, Cluster *cls, PortGeneric *aGen, LSQ *lsq, StoreSet *ss, Prefetcher *pref, SCB* scb, TimeDelta_t l,
              GMemorySystem *ms, int32_t id, const char *cad);

public:

};

class FULoad : public MemResource {
private:
  const TimeDelta_t LSDelay;

  int32_t freeEntries;
  bool    enableDcache;
#ifdef MEM_TSO2
  GStatsCntr tso2Replay;
#endif

protected:
  void                                                               cacheDispatched(DInst *dinst);
  typedef CallbackMember1<FULoad, DInst *, &FULoad::cacheDispatched> cacheDispatchedCB;

public:
  FULoad(uint8_t type, Cluster *cls, PortGeneric *aGen, LSQ *lsq, StoreSet *ss, Prefetcher *pref, SCB* scb,  TimeDelta_t lsdelay, TimeDelta_t l, GMemorySystem *ms, int32_t size, int32_t id, const char *cad);

  StallCause canIssue(DInst *dinst);
  void       executing(DInst *dinst);
  void       executed(DInst *dinst);
  bool       preretire(DInst *dinst, bool flushing);
  bool       retire(DInst *dinst, bool flushing);
  void       performed(DInst *dinst);
  
  bool       isLoadSpec( DInst *dinst);
};

class FUStore : public MemResource {
private:
  int32_t  freeEntries;
  bool     enableDcache;
  int32_t  scbSize;
  int32_t  scbEntries;
 // int32_t  scbMerge[1024];
  uint32_t lineSizeBits;

  //typedef std::list<DInst *> SCBQueueType;
 // SCBQueueType               scbQueue;



public:
  FUStore(uint8_t type, Cluster *cls, PortGeneric *aGen, LSQ *lsq, StoreSet *ss, Prefetcher *pref, SCB* scb, TimeDelta_t l, GMemorySystem *ms,
          int32_t size, int32_t id, const char *cad);

  StallCause canIssue(DInst *dinst);
  void       executing(DInst *dinst);
  void       executed(DInst *dinst);
  bool       preretire(DInst *dinst, bool flushing);
  bool       retire(DInst *dinst, bool flushing);
  void       performed(DInst *dinst);
};

class FUGeneric : public Resource {
private:
protected:
public:
  FUGeneric(uint8_t type, Cluster *cls, PortGeneric *aGen, TimeDelta_t l, uint32_t cpuid);

  StallCause canIssue(DInst *dinst);
  void       executing(DInst *dinst);
  void       executed(DInst *dinst);
  bool       preretire(DInst *dinst, bool flushing);
  bool       retire(DInst *dinst, bool flushing);
  void       performed(DInst *dinst);
};

class FUBranch : public Resource {
private:
  int32_t freeBranches;
  bool    drainOnMiss;

protected:
public:
  FUBranch(uint8_t type, Cluster *cls, PortGeneric *aGen, TimeDelta_t l, uint32_t cpuid, int32_t mb, bool dom);

  StallCause canIssue(DInst *dinst);
  void       executing(DInst *dinst);
  void       executed(DInst *dinst);
  bool       preretire(DInst *dinst, bool flushing);
  bool       retire(DInst *dinst, bool flushing);
  void       performed(DInst *dinst);
};

class FURALU : public Resource {
private:
  GStatsCntr dmemoryBarrier;
  GStatsCntr imemoryBarrier;
  Time_t     blockUntil;

protected:
public:
  FURALU(uint8_t type, Cluster *cls, PortGeneric *aGen, TimeDelta_t l, int32_t id);

  StallCause canIssue(DInst *dinst);
  void       executing(DInst *dinst);
  void       executed(DInst *dinst);
  bool       preretire(DInst *dinst, bool flushing);
  bool       retire(DInst *dinst, bool flushing);
  void       performed(DInst *dinst);
};

