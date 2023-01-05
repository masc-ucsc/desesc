// See LICENSE for details.

#include <limits.h>

#include "fmt/format.h"

// #define MEM_TSO 1
// #define MEM_TSO2 1
#include "Cluster.h"
#include "FetchEngine.h"
#include "GMemorySystem.h"
#include "GProcessor.h"
#include "LSQ.h"
#include "MemRequest.h"
#include "OoOProcessor.h"
#include "Resource.h"
#include "dinst.hpp"
#include "port.hpp"

// late allocation flag
#define USE_PNR
#define LSQ_LATE_EXECUTED 1

/* }}} */

Resource::Resource(uint8_t type, Cluster *cls, PortGeneric *aGen, TimeDelta_t l, uint32_t cpuid)
    /* constructor {{{1 */
    : cluster(cls)
    , gen(aGen)
    , avgRenameTime(fmt::format("({})_{}_{}_avgRenameTime", cpuid, cls->getName(), type))
    , avgIssueTime(fmt::format("({})_{}_{}_avgIssueTime", cpuid, cls->getName(), type))
    , avgExecuteTime(fmt::format("({})_{}_{}_avgExecuteTime", cpuid, cls->getName(), type))
    , avgRetireTime(fmt::format("({})_{}_{}_avgRetireTime", cpuid, cls->getName(), type))
    , safeHitTimeHist(fmt::format("({})_{}_{}_safeHitTimeHist", cpuid, cls->getName(), type))
    , specHitTimeHist(fmt::format("({})_{}_{}_specHitTimeHist", cpuid, cls->getName(), type))
    , latencyHitTimeHist(fmt::format("({})_{}_{}_latencyHitTimeHist", cpuid, cls->getName(), type))
    , lat(l)
    , coreid(cpuid)
    , usedTime(0) {
  I(cls);
  if (gen) {
    gen->subscribe();
  }

  auto core_type = Config::get_string("soc", "core", cpuid, "type", {"inorder", "ooo", "accel"});
  std::transform(core_type.begin(), core_type.end(), core_type.begin(), [](unsigned char c) { return std::toupper(c); });

  inorder = (core_type != "ooo");
}
/* }}} */

void Resource::setStats(const Dinst *dinst) {
  if (!dinst->has_stats()) {
    return;
  }

  Time_t t;

  t = globalClock - dinst->getExecutedTime();
  avgRetireTime.sample(t, true);

  t = dinst->getExecutedTime() - dinst->getIssuedTime();
  avgExecuteTime.sample(t, true);

  t = dinst->getIssuedTime() - dinst->getRenamedTime();
  avgIssueTime.sample(t, true);

  t = dinst->getRenamedTime() - dinst->getFetchTime();
  avgRenameTime.sample(t, true);
}

Resource::~Resource()
/* destructor {{{1 */
{
  if (!EventScheduler::empty()) {
    fmt::print("Resources destroyed with {} pending instructions (small is OK)\n", EventScheduler::size());
  }

  if (gen) {
    gen->unsubscribe();
  }
}
/* }}} */

/***********************************************/

MemResource::MemResource(uint8_t type, Cluster *cls, PortGeneric *aGen, LSQ *_lsq, std::shared_ptr<StoreSet> ss,
                         std::shared_ptr<Prefetcher> _pref, std::shared_ptr<Store_buffer> _scb, TimeDelta_t l,
                         std::shared_ptr<GMemorySystem> ms, int32_t id, const char *cad)
    /* constructor {{{1 */
    : MemReplay(type, cls, aGen, ss, l, id)
    , firstLevelMemObj(ms->getDL1())
    , memorySystem(ms)
    , lsq(_lsq)
    , pref(_pref)
    , scb(_scb)
    , stldViolations(fmt::format("P({})_{}_{}:stldViolations", id, cls->getName(), cad)) {
  if (firstLevelMemObj->get_type() == "cache") {
    DL1 = firstLevelMemObj;
  } else {
    MRouter *router = firstLevelMemObj->getRouter();
    DL1             = router->getDownNode();
    if (DL1->get_type() == "cache") {
      MRouter *mr = DL1->getRouter();
      DL1         = mr->getDownNode();
      if (DL1->get_type() != "cache") {
        Config::add_error(fmt::format("neither first or second or 3rd level is a cache {}", DL1->get_type()));
      }
    }
  }
}

/* }}} */

MemReplay::MemReplay(uint8_t type, Cluster *cls, PortGeneric *_gen, std::shared_ptr<StoreSet> ss, TimeDelta_t l, uint32_t cpuid)
    : Resource(type, cls, _gen, l, cpuid), lfSize(8), storeset(ss) {
  lf = (struct FailType *)malloc(sizeof(struct FailType) * lfSize);
  for (uint32_t i = 0; i < lfSize; i++) {
    lf[i].ssid = -1;
  }
}

void MemReplay::replayManage(Dinst *dinst) {
  if (dinst->getSSID() == -1) {
    return;
  }

  int  pos     = -1;
  int  pos2    = 0;
  bool updated = false;

  SSID_t did = dinst->getSSID();

  for (uint32_t i = 0; i < lfSize; i++) {
    if (lf[pos2].id < lf[i].id && lf[i].ssid != did) {
      pos2 = i;
    }

    if (lf[i].id >= dinst->getID()) {
      continue;
    }
    if (lf[i].ssid == -1) {
      if (!updated) {
        lf[i].ssid = did;
        lf[i].id   = dinst->getID();
        lf[i].pc   = dinst->getPC();
        lf[i].addr = dinst->getAddr();
        lf[i].op   = dinst->getInst()->getOpcode();
        updated    = true;
      }
      continue;
    }
    if ((dinst->getID() - lf[i].id) > 500) {
      if (!updated) {
        lf[i].ssid = did;
        lf[i].id   = dinst->getID();
        lf[i].pc   = dinst->getPC();
        lf[i].addr = dinst->getAddr();
        lf[i].op   = dinst->getInst()->getOpcode();
        updated    = true;
      }
      continue;
    }
    if (lf[i].ssid == did) {
      continue;
    }
    if (lf[i].addr != dinst->getAddr()) {
      if (pos == -1) {
        pos = i;
      }
      continue;
    }

    if (pos != -3) {
      pos = -2;
#if 0
      printf("1.merging %d and %d : pc %x and %x : addr %x and %x : %dx%d : data %x and %x : id %d and %d (%d) : %x\n"
             ,lf[i].ssid, did, lf[i].pc, dinst->getPC(), lf[i].addr, dinst->getAddr()
             ,lf[i].op, dinst->getInst()->getOpcode()
             ,lf[i].data, dinst->getData(), lf[i].id, dinst->getID(), dinst->getID() - lf[i].id, dinst->getConflictStorePC());
#endif

      SSID_t newid = storeset->mergeset(lf[i].ssid, did);
      did          = newid;
      lf[i].ssid   = newid;
      lf[i].id     = dinst->getID();
      lf[i].pc     = dinst->getPC();
      lf[i].addr   = dinst->getAddr();
      lf[i].op     = dinst->getInst()->getOpcode();
      updated      = true;
    }
  }

  static int rand = 0;
  rand++;
#if 1
  if (pos >= 0 && (rand & 7) == 0) {
    int i = pos;
#if 0
      printf("2.merging %d and %d : pc %x and %x : addr %x and %x : %dx%d : data %x and %x : id %d and %d (%d) : %x\n"
          ,lf[i].ssid, dinst->getSSID(), lf[i].pc, dinst->getPC(), lf[i].addr, dinst->getAddr()
          , lf[i].op, dinst->getInst()->getOpcode()
          , lf[i].data, dinst->getData(), lf[i].id, dinst->getID(), dinst->getID() - lf[i].id, dinst->getConflictStorePC());
#endif
    SSID_t newid = storeset->mergeset(lf[i].ssid, dinst->getSSID());
    lf[i].ssid   = newid;
    lf[i].id     = dinst->getID();
    lf[i].pc     = dinst->getPC();
    lf[i].addr   = dinst->getAddr();
    updated      = true;
  }
#endif

  if (!updated) {
    int i = pos2;
    if (dinst->getID() > lf[i].id && dinst->getSSID() != lf[i].ssid) {
#ifndef NDEBUG
      fmt::print("3.merging {} and {} : pc {} and {} : addr {} and {} : id {u and {} ({})\n",
                 lf[i].ssid,
                 dinst->getSSID(),
                 lf[i].pc,
                 dinst->getPC(),
                 lf[i].addr,
                 dinst->getAddr(),
                 lf[i].id,
                 dinst->getID(),
                 dinst->getID() - lf[i].id);
#endif
      storeset->mergeset(lf[i].ssid, dinst->getSSID());
      if (lf[i].ssid > dinst->getSSID()) {
        lf[i].ssid = dinst->getSSID();
      }

      lf[i].ssid = dinst->getSSID();
      lf[i].id   = dinst->getID();
      lf[i].pc   = dinst->getPC();
      lf[i].addr = dinst->getAddr();
    }
  }
}

/***********************************************/

FULoad::FULoad(uint8_t type, Cluster *cls, PortGeneric *aGen, LSQ *_lsq, std::shared_ptr<StoreSet> ss,
               std::shared_ptr<Prefetcher> _pref, std::shared_ptr<Store_buffer> _scb, TimeDelta_t lsdelay, TimeDelta_t l,
               std::shared_ptr<GMemorySystem> ms, int32_t size, int32_t id, const char *cad)
    /* Constructor {{{1 */
    : MemResource(type, cls, aGen, _lsq, ss, _pref, _scb, l, ms, id, cad)
#ifdef MEM_TSO2
    , tso2Replay("P(%d)_%s_%s:tso2Replay", id, cls->getName(), cad)
#endif
    , LSDelay(lsdelay)
    , freeEntries(size) {
  I(ms);

  if (Config::get_bool("soc", "core", id, "caches")) {
    auto dl1_sec      = Config::get_string("soc", "core", id, "dl1");
    auto dl1_sec_type = Config::get_string(dl1_sec, "type", {"cache", "nice", "bus"});
    enableDcache      = dl1_sec_type != "nice";
  }else{
    enableDcache      = false;
  }

  LSQlateAlloc = Config::get_bool("soc", "core", id, "ldq_late_alloc");
}
/* }}} */

StallCause FULoad::canIssue(Dinst *dinst) {
  /* canIssue {{{1 */

  if (freeEntries <= 0) {
    I(freeEntries == 0);  // Can't be negative
    return OutsLoadsStall;
  }
  if (!lsq->hasFreeEntries()) {
    return OutsLoadsStall;
  }

  if (inorder) {
    if (lsq->hasPendingResolution()) {
      return OutsLoadsStall;
    }
  }

  if (!lsq->insert(dinst)) {
    return OutsLoadsStall;
  }

  storeset->insert(dinst);
  // call vtage->rename() here????

  lsq->decFreeEntries();

  if (!LSQlateAlloc) {
    freeEntries--;
  }
  return NoStall;
}
/* }}} */

void FULoad::executing(Dinst *dinst) {
  /* executing {{{1 */

  if (LSQlateAlloc) {
    freeEntries--;
  }

  cluster->executing(dinst);
  Time_t when = gen->nextSlot(dinst->has_stats()) + lat;

  Dinst *qdinst = lsq->executing(dinst);
  I(qdinst == 0);
  if (qdinst) {
    I(qdinst->getInst()->isStore());
    dinst->getGProc()->replay(dinst);
    if (!dinst->getGProc()->is_nuking()) {
      stldViolations.inc(dinst->has_stats());
    }

    storeset->stldViolation(qdinst, dinst);
  }

  if (dinst->isLoadForwarded() || scb->is_ld_forward(dinst->getAddr()) || !enableDcache) {
    performedCB::scheduleAbs(when + LSDelay, this, dinst);
    dinst->markDispatched();

    pref->exe(dinst);
  } else {
    cacheDispatchedCB::scheduleAbs(when, this, dinst);
  }
}

/* }}} */

void FULoad::cacheDispatched(Dinst *dinst) {
  /* cacheDispatched {{{1 */

  I(enableDcache);
  I(!dinst->isLoadForwarded());

  dinst->markDispatched();

  if (false && dinst->is_spec()) {  // Future Spectre Related
    MemRequest::sendSpecReqDL1Read(firstLevelMemObj,
                                   dinst->has_stats(),
                                   dinst->getAddr(),
                                   dinst->getPC(),
                                   dinst,
                                   performedCB::create(this, dinst));
  } else {
    MemRequest::sendSafeReqDL1Read(firstLevelMemObj,
                                   dinst->has_stats(),
                                   dinst->getAddr(),
                                   dinst->getPC(),
                                   dinst,
                                   performedCB::create(this, dinst));
  }

  pref->exe(dinst);
}
/* }}} */

void FULoad::executed(Dinst *dinst) {
  /* executed {{{1 */

  if (dinst->getChained()) {
    I(dinst->getFetchEngine());
    dinst->getFetchEngine()->chainLoadDone(dinst);
  } else {
    I(!dinst->getFetchEngine());
  }

  storeset->remove(dinst);

  /*************************/
  // HERE: Do a call to mark the LD as predicted (index the and set if dataRATptr is to dinst then
  // dataRAT[dinst->getinst()->getDst1()] = dinst->getData();)
  //         dataRATptr[...] = 0 if dataRATotr[...] == dinst

  cluster->executed(dinst);
}
/* }}} */

bool FULoad::preretire(Dinst *dinst, [[maybe_unused]] bool flushing)
/* retire {{{1 */
{
  bool done = dinst->isDispatched();
  if (!done) {
    return false;
  }

#ifdef USE_PNR
  freeEntries++;
#endif

  pref->ret(dinst);

  return true;
}
/* }}} */

bool FULoad::retire(Dinst *dinst, [[maybe_unused]] bool flushing)
/* retire {{{1 */
{
  if (!dinst->isPerformed()) {
    return false;
  }

  lsq->incFreeEntries();
#ifndef USE_PNR
  freeEntries++;
#endif

#ifdef MEM_TSO2
  if (DL1->Invalid(dinst->getAddr())) {
    // MSG("Sync head/tail @%lld",globalClock);
    tso2Replay.inc(dinst->has_stats());
    dinst->getGProc()->replay(dinst);
  }
#endif

  lsq->remove(dinst);
  // VTAGE->updateVtageTables() here ???? vtage validation

#if 0
  // Merging for tradcore
  if(dinst->isReplay() && !flushing)
    replayManage(dinst);
#endif

  setStats(dinst);

  return true;
}
/* }}} */

void FULoad::performed(Dinst *dinst) {
  /* memory operation was globally performed {{{1 */
  dinst->markPerformed();

  executed(dinst);
}
/* }}} */

/***********************************************/

FUStore::FUStore(uint8_t type, Cluster *cls, PortGeneric *aGen, LSQ *_lsq, std::shared_ptr<StoreSet> ss,
                 std::shared_ptr<Prefetcher> _pref, std::shared_ptr<Store_buffer> _scb, TimeDelta_t l,
                 std::shared_ptr<GMemorySystem> ms, int32_t size, int32_t id, const char *cad)
    /* constructor {{{1 */
    : MemResource(type, cls, aGen, _lsq, ss, _pref, _scb, l, ms, id, cad), freeEntries(size) {
  if (Config::get_bool("soc", "core", id, "caches")) {
    auto dl1_sec      = Config::get_string("soc", "core", id, "dl1");
    auto dl1_sec_type = Config::get_string(dl1_sec, "type", {"cache", "nice", "bus"});
    enableDcache      = dl1_sec_type != "nice";
  }else{
    enableDcache      = false;
  }

  LSQlateAlloc = Config::get_bool("soc", "core", id, "stq_late_alloc");
}
/* }}} */

StallCause FUStore::canIssue(Dinst *dinst) {
  /* canIssue {{{1 */

  if (dinst->getInst()->isStoreAddress()) {
    return NoStall;
  }

  if (freeEntries <= 0) {
    I(freeEntries == 0);  // Can't be negative
    return OutsStoresStall;
  }
  if (!lsq->hasFreeEntries()) {
    return OutsStoresStall;
  }

  if (inorder) {
    if (lsq->hasPendingResolution()) {
      return OutsStoresStall;
    }
  }

  if (!lsq->insert(dinst)) {
    return OutsStoresStall;
  }

  storeset->insert(dinst);

  lsq->decFreeEntries();
  freeEntries--;

  return NoStall;
}
/* }}} */

void FUStore::executing(Dinst *dinst) {
  /* executing {{{1 */

  if (!dinst->getInst()->isStoreAddress()) {
    Dinst *qdinst = lsq->executing(dinst);
    if (qdinst) {
      dinst->getGProc()->replay(qdinst);
      if (!dinst->getGProc()->is_nuking()) {
        stldViolations.inc(dinst->has_stats());
      }
      storeset->stldViolation(qdinst, dinst);
    }
  }

  cluster->executing(dinst);
  gen->nextSlot(dinst->has_stats());

  if (dinst->getInst()->isStoreAddress()) {
#if 0
    if (enableDcache && !firstLevelMemObj->isBusy(dinst->getAddr()) ){
      MemRequest::sendReqWritePrefetch(firstLevelMemObj, dinst->has_stats(), dinst->getAddr(), 0); // executedCB::create(this,dinst));
    }
    executed(dinst);
#else
    executed(dinst);
#endif
  } else {
    executed(dinst);
  }
}
/* }}} */

void FUStore::executed(Dinst *dinst) {
  /* executed */

  if (dinst->getInst()->isStore()) {
    storeset->remove(dinst);
  }

  cluster->executed(dinst);
}

bool FUStore::preretire(Dinst *dinst, bool flushing) {
  /* retire {{{1 */

  if (!dinst->isExecuted()) {
    return false;
  }
  if (dinst->getInst()->isStoreAddress()) {
    return true;
  }
  if (flushing) {
    performed(dinst);
    return true;
  }
  if (!scb->can_accept_st(dinst->getAddr())) {
    return false;
  }

  if (firstLevelMemObj->isBusy(dinst->getAddr())) {
    return false;
  }

  scb->add_st(dinst);

  if (enableDcache) {
    MemRequest::sendReqWrite(firstLevelMemObj,
                             dinst->has_stats(),
                             dinst->getAddr(),
                             dinst->getPC(),
                             performedCB::create(this, dinst));
  } else {
    performed(dinst);
  }

  freeEntries++;

  return true;
}
/* }}} */

void FUStore::performed(Dinst *dinst) {
  /* memory operation was globally performed {{{1 */

  setStats(dinst);  // Not retire for stores

  I(!dinst->isPerformed());
  if (dinst->isRetired()) {
    dinst->recycle();
  }
  dinst->markPerformed();
}
/* }}} */

bool FUStore::retire(Dinst *dinst, bool flushing) {
  (void)flushing;

  if (dinst->getInst()->isStoreAddress()) {
    setStats(dinst);
    return true;
  }

  I(!dinst->isRetired());
  dinst->markRetired();

  dinst->clearRATEntry();  // Stores could have dependences for LL and atomics. Clear at retire

  lsq->remove(dinst);
  lsq->incFreeEntries();

  return true;
}

/***********************************************/

FUGeneric::FUGeneric(uint8_t type, Cluster *cls, PortGeneric *aGen, TimeDelta_t l, uint32_t cpuid)
    /* constructor {{{1 */
    : Resource(type, cls, aGen, l, cpuid) {}
/* }}} */

StallCause FUGeneric::canIssue(Dinst *dinst) {
  (void)dinst;
#if 0
  if (inorder) {
    Time_t t = gen->calcNextSlot();
    if (t>globalClock)
      return DivergeStall;
  }
#endif
  return NoStall;
}

void FUGeneric::executing(Dinst *dinst) {
  /* executing {{{1 */
  Time_t nlat = gen->nextSlot(dinst->has_stats()) + lat;
#if 0
  if (dinst->getPC() == 1073741832) {
    MSG("@%lld Scheduling callback for FID[%d] PE[%d] Warp [%d] pc 1073741832 at @%lld"
        , (long long int)globalClock
        , dinst->getFlowId()
        , dinst->getPE()
        , dinst->getWarpID()
        , (long long int) nlat);
  }
#endif
  cluster->executing(dinst);
  executedCB::scheduleAbs(nlat, this, dinst);
}
/* }}} */

void FUGeneric::executed(Dinst *dinst) {
  /* executed {{{1 */
  cluster->executed(dinst);
  dinst->markPerformed();
}
/* }}} */

bool FUGeneric::preretire(Dinst *dinst, bool flushing)
/* preretire {{{1 */
{
  (void)flushing;
  return dinst->isExecuted();
}
/* }}} */

bool FUGeneric::retire(Dinst *dinst, bool flushing)
/* retire {{{1 */
{
  (void)flushing;
  setStats(dinst);

  return true;
}
/* }}} */

void FUGeneric::performed(Dinst *dinst) {
  /* memory operation was globally performed {{{1 */
  dinst->markPerformed();
  I(0);  // It should be called only for memory operations
}
/* }}} */

/***********************************************/

FUBranch::FUBranch(uint8_t type, Cluster *cls, PortGeneric *aGen, TimeDelta_t l, uint32_t cpuid, int32_t mb, bool dom)
    /* constructor {{{1 */
    : Resource(type, cls, aGen, l, cpuid), freeBranches(mb), drainOnMiss(dom) {
  I(freeBranches > 0);
}
/* }}} */

StallCause FUBranch::canIssue(Dinst *dinst) {
  /* canIssue {{{1 */

  I(dinst->getInst()->isControl());
  if (freeBranches == 0) {
    return OutsBranchesStall;
  }

  freeBranches--;

  return NoStall;
}
/* }}} */

void FUBranch::executing(Dinst *dinst) {
  /* executing {{{1 */
  cluster->executing(dinst);
  executedCB::scheduleAbs(gen->nextSlot(dinst->has_stats()) + lat, this, dinst);
}
/* }}} */

void FUBranch::executed(Dinst *dinst) {
  /* executed {{{1 */
  cluster->executed(dinst);
  dinst->markPerformed();

  if (!drainOnMiss && dinst->isBranchMiss()) {
    (dinst->getFetchEngine())->unBlockFetch(dinst, dinst->getFetchTime());
  }

  // NOTE: assuming that once the branch is executed the entry can be recycled
  freeBranches++;
}
/* }}} */

bool FUBranch::preretire(Dinst *dinst, bool flushing)
/* preretire {{{1 */
{
  (void)flushing;
  if (drainOnMiss && dinst->isExecuted() && dinst->isBranchMiss()) {
    (dinst->getFetchEngine())->unBlockFetch(dinst, dinst->getFetchTime());
  }
  return dinst->isExecuted();
}
/* }}} */

bool FUBranch::retire(Dinst *dinst, [[maybe_unused]] bool flushing)
/* retire {{{1 */
{
  setStats(dinst);
  return true;
}
/* }}} */

void FUBranch::performed(Dinst *dinst) {
  /* memory operation was globally performed {{{1 */
  dinst->markPerformed();
  I(0);  // It should be called only for memory operations
}
/* }}} */

/***********************************************/

FURALU::FURALU(uint8_t type, Cluster *cls, PortGeneric *aGen, TimeDelta_t l, int32_t id)
    /* constructor {{{1 */
    : Resource(type, cls, aGen, l, id)
    , dmemoryBarrier(fmt::format("P({})_{}_dmemoryBarrier", id, cls->getName()))
    , imemoryBarrier(fmt::format("P({})_{}_imemoryBarrier", id, cls->getName())) {
  blockUntil = 0;
}
/* }}} */

StallCause FURALU::canIssue(Dinst *dinst)
/* canIssue {{{1 */
{
  I(dinst->getPC() != 0xf00df00d);  // It used to be a Syspend, but not longer true

  if (dinst->getPC() == 0xdeaddead) {
    // This is the PC for a syscall (QEMUReader::syscall)
    if (blockUntil == 0) {
      // LOG("syscall %d executed, with %d delay", dinst->getAddr(), dinst->getData());
      // blockUntil = globalClock+dinst->getData();
      blockUntil = globalClock + 100;
      return SyscallStall;
    }

    // is this where we poweron the GPU threads and then poweroff the QEMU thread?
    if (globalClock >= blockUntil) {
      blockUntil = 0;
      return NoStall;
    }

    return SyscallStall;
  } else if (!dinst->getInst()->hasDstRegister() && !dinst->getInst()->hasSrc1Register() && !dinst->getInst()->hasSrc2Register()) {
    if (dinst->getGProc()->isROBEmpty()) {
      return NoStall;
    }
    if (dinst->getAddr() == 0xbeefbeef) {
      imemoryBarrier.inc(dinst->has_stats());
    } else {
      dmemoryBarrier.inc(dinst->has_stats());
    }
    // FIXME return SyscallStall;
  }

  return NoStall;
}
/* }}} */

void FURALU::executing(Dinst *dinst)
/* executing {{{1 */
{
  cluster->executing(dinst);
  executedCB::scheduleAbs(gen->nextSlot(dinst->has_stats()) + lat, this, dinst);

  // Recommended poweron the GPU threads and then poweroff the QEMU thread?
}
/* }}} */

void FURALU::executed(Dinst *dinst)
/* executed {{{1 */
{
  cluster->executed(dinst);
  dinst->markPerformed();
}
/* }}} */

bool FURALU::preretire(Dinst *dinst, [[maybe_unused]] bool flushing)
/* preretire {{{1 */
{
  return dinst->isExecuted();
}
/* }}} */

bool FURALU::retire(Dinst *dinst, [[maybe_unused]] bool flushing)
/* retire {{{1 */
{
  setStats(dinst);
  return true;
}
/* }}} */

void FURALU::performed(Dinst *dinst)
/* memory operation was globally performed {{{1 */
{
  dinst->markPerformed();
  I(0);  // It should be called only for memory operations
}
/* }}} */
