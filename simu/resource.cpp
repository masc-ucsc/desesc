// See LICENSE for details.

#include <limits.h>

#include "fmt/format.h"

// #define MEM_TSO 1
// #define MEM_TSO2 1
#include "cluster.hpp"
#include "dinst.hpp"
#include "fetchengine.hpp"
#include "gmemory_system.hpp"
#include "gprocessor.hpp"
#include "lsq.hpp"
#include "memrequest.hpp"
#include "oooprocessor.hpp"
#include "port.hpp"
#include "resource.hpp"
#include "tracer.hpp"

// SCB SPEC buffer : directly L3->SCB //#define ENABLE_SCB_SPEC
#define ENABLE_SCB_SPEC
// SCB: Basic on for all stores and all loads: normalSCB
#define ENABLE_SCB_ALL

// late allocation flag
#define USE_PNR

/* }}} */

Resource::Resource(Opcode type, std::shared_ptr<Cluster> cls, std::shared_ptr<PortGeneric> aGen, TimeDelta_t l, uint32_t cpuid)
    /* constructor {{{1 */
    : cluster(cls)
    , gen(aGen)
    , avgRenameTime(fmt::format("P({})_{}_{}_avgRenameTime", cpuid, cls->getName(), type))
    , avgIssueTime(fmt::format("P({})_{}_{}_avgIssueTime", cpuid, cls->getName(), type))
    , avgExecuteTime(fmt::format("P({})_{}_{}_avgExecuteTime", cpuid, cls->getName(), type))
    , avgRetireTime(fmt::format("P({})_{}_{}_avgRetireTime", cpuid, cls->getName(), type))
    , safeHitTimeHist(fmt::format("P({})_{}_{}_safeHitTimeHist", cpuid, cls->getName(), type))
    , specHitTimeHist(fmt::format("P({})_{}_{}_specHitTimeHist", cpuid, cls->getName(), type))
    , latencyHitTimeHist(fmt::format("P({})_{}_{}_latencyHitTimeHist", cpuid, cls->getName(), type))
    , lat(l)
    , coreid(cpuid)
    , usedTime(0) {
  I(cls);

  auto core_type = Config::get_string("soc", "core", cpuid, "type", {"inorder", "ooo", "accel"});
  std::transform(core_type.begin(), core_type.end(), core_type.begin(), [](unsigned char c) { return std::toupper(c); });
  // correct is (core_type != "OOO")

  // BUG_was:inorder = (core_type != "ooo");
  // inorder = (core_type != "ooo");
  // correct uppercase OOO
  inorder = (core_type != "OOO");
}
/* }}} */

void Resource::setStats(const Dinst* dinst) {
  if (!dinst->has_stats() || dinst->isTransient()) {
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
  if (EventScheduler::size() > 10) {
    fmt::print("Resources destroyed with {} pending instructions (small is OK)\n", EventScheduler::size());
  }
}
/* }}} */

/***********************************************/

MemResource::MemResource(Opcode type, std::shared_ptr<Cluster> cls, std::shared_ptr<PortGeneric> aGen, LSQ* _lsq,
                         std::shared_ptr<StoreSet> ss, std::shared_ptr<Prefetcher> _pref, std::shared_ptr<Store_buffer> _scb,
                         TimeDelta_t l, std::shared_ptr<Gmemory_system> ms, int32_t id, const std::string& cad)
    /* constructor {{{1 */
    : MemReplay(type, cls, aGen, ss, l, id)
    , firstLevelMemObj(ms->getDL1())
    , lsq(_lsq)
    , pref(_pref)
    , scb(_scb)
    , stldViolations(fmt::format("P({})_{}_{}:stldViolations", id, cls->getName(), cad)) {
  if (firstLevelMemObj->get_type() == "cache" || firstLevelMemObj->get_type() == "nice") {
    DL1 = firstLevelMemObj;
  } else {
    MRouter* router = firstLevelMemObj->getRouter();
    DL1             = router->getDownNode();
    if (DL1->get_type() == "cache" || DL1->get_type() == "nice") {
      MRouter* mr = DL1->getRouter();
      DL1         = mr->getDownNode();
      if (DL1->get_type() != "cache") {
        Config::add_error(fmt::format("neither first or second or 3rd level is a cache {}", DL1->get_type()));
      }
    }
  }
}

/* }}} */

MemReplay::MemReplay(Opcode type, std::shared_ptr<Cluster> cls, std::shared_ptr<PortGeneric> _gen, std::shared_ptr<StoreSet> ss,
                     TimeDelta_t l, uint32_t cpuid)
    : Resource(type, cls, _gen, l, cpuid), lfSize(8), storeset(ss) {
  lf.resize(lfSize);
}

void MemReplay::replayManage(Dinst* dinst) {
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
  if (pos >= 0 && (rand & 7) == 0) {
    int    i     = pos;
    SSID_t newid = storeset->mergeset(lf[i].ssid, dinst->getSSID());
    lf[i].ssid   = newid;
    lf[i].id     = dinst->getID();
    lf[i].pc     = dinst->getPC();
    lf[i].addr   = dinst->getAddr();
    updated      = true;
  }

  if (!updated) {
    int i = pos2;
    if (dinst->getID() > lf[i].id && dinst->getSSID() != lf[i].ssid) {
#ifndef NDEBUG
      fmt::print("3.merging {} and {} : pc {} and {} : addr {} and {} : id {} and {} ({})\n",
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

FULoad::FULoad(Opcode type, std::shared_ptr<Cluster> cls, std::shared_ptr<PortGeneric> aGen, LSQ* _lsq,
               std::shared_ptr<StoreSet> ss, std::shared_ptr<Prefetcher> _pref, std::shared_ptr<Store_buffer> _scb,
               TimeDelta_t lsdelay, TimeDelta_t l, std::shared_ptr<Gmemory_system> ms, int32_t size, int32_t id,
               const std::string& cad)
    /* Constructor {{{1 */
    : MemResource(type, cls, aGen, _lsq, ss, _pref, _scb, l, ms, id, cad)
#ifdef MEM_TSO2
    , tso2Replay("P(%d)_%s_%s:tso2Replay", id, cls->getName(), cad)
#endif
    , LSDelay(lsdelay)
    , freeEntries(size) {
  I(ms);

  enableDcache = Config::get_bool("soc", "core", id, "caches");

  LSQlateAlloc = Config::get_bool("soc", "core", id, "ldq_late_alloc");
}
/* }}} */

StallCause FULoad::canIssue(Dinst* dinst) {
  /* canIssue {{{1 */
  // printf("FULoad::Resource::canIssue():: Entering Canissue() dinst  %llu\n", dinst->getID());

  // dinst->set_scb(scb);
  if (freeEntries <= 0) {
    I(freeEntries == 0);  // Can't be negative
    return OutsLoadsStall;
  }

  if (!lsq->hasFreeEntries()) {
    return OutsLoadsStall;
  }

  // LSQ::unresloved>0 ; inorder='uppercase OOO"
  if (inorder) {
    if (lsq->hasPendingResolution()) {
      return OutsLoadsStall;
    }
  }

  /* Insert dinst in LSQFull (in-order) */
  if (!lsq->insert(dinst)) {
    return OutsLoadsStall;
  }

  /*CanIssue starts from here */
  storeset->insert(dinst);
  // call vtage->rename() here????

  lsq->decFreeEntries();

  if (!LSQlateAlloc) {
    freeEntries--;
  }

  // printf("FULoad::Resource::canIssue():: successfully Canissue() dinst  %llu\n", dinst->getID());
  return NoStall;
}
/* }}} */

void FULoad::executing(Dinst* dinst) {
  /* executing {{{1 */
  gen->schedule(dinst->has_stats(),
                dinst->getID(),
                dinst->isTransient(),
                [this, dinst](Time_t allocated_time) { do_load_execution(allocated_time, dinst); });
}

/* }}} */

void FULoad::do_load_execution(Time_t when, Dinst* dinst) {
  if (LSQlateAlloc) {
    freeEntries--;
  }

  cluster->executing(dinst);

  Time_t when_sched = when + lat;

  Dinst* qdinst = lsq->executing(dinst);
  I(qdinst == 0);
  if (qdinst) {
    I(qdinst->getInst()->isStore());
    dinst->getGProc()->replay(dinst);
    if (!dinst->getGProc()->is_nuking()) {
      stldViolations.inc(dinst->has_stats());
    }
    storeset->stldViolation(qdinst, dinst);
  }

#ifdef ENABLE_SCB_SPEC
  if (dinst->isLoadForwarded() || scb->is_ld_forward(dinst->getAddr()) || !enableDcache || dinst->is_destroy_transient())
#else
  if (dinst->isLoadForwarded() || !enableDcache || dinst->is_destroy_transient() || scb->is_ld_forward(dinst->getAddr()))
#endif
  {
    // printf("FULoad::Resource::Executing::isLoadForwared::cache::dinst  %llu\n", dinst->getID());
    if (dinst->is_spec()) {  // Future Spectre Related
#ifdef ENABLE_SCB_SPEC
      performed_spec_CB::scheduleAbs(when + LSDelay, this, dinst);
      dinst->markDispatched();
#endif
#ifndef ENABLE_SCB_SPEC
      dinst->set_load_scb_all();
      performedCB::scheduleAbs(when + LSDelay, this, dinst);
      dinst->markDispatched();
#endif
    } else if (dinst->is_safe()) {
      dinst->set_load_scb_all();
      performedCB::scheduleAbs(when + LSDelay, this, dinst);
      dinst->markDispatched();

      pref->exe(dinst);
    }
  } else {
    /* }}} */

    cacheDispatchedCB::scheduleAbs(when_sched, this, dinst, dinst->getID());
  }
}

void FULoad::cacheDispatched(Dinst* dinst) {
  /* cacheDispatched {{{1 */

  // printf("Resource::cacheDispatched::Entering CacheDispatched dinst  %llu\n", dinst->getID());
  I(enableDcache);
  I(!dinst->isLoadForwarded());

  dinst->markDispatched();
  // make sure retire() do not destroy befor the performcb comes::
  // mark->retired();
  if (dinst->is_spec()) {  // Future Spectre Related
#ifdef ENABLE_SCB_SPEC
    // printf("Resource::cacheDispatched::SPEC_LOAD_SCB_SPEC::Performed_spec_CB::sendSpecL1LoadREAD cache::dinst  %llu\n",
           // dinst->getID());
    // printf("FULoad::Resource::Cachedispatched::dinst  %llu and clock cycle %llu \n", dinst->getID(), globalClock);
    MemRequest::sendSpecReqDL1Read(firstLevelMemObj,
                                   dinst->has_stats(),
                                   dinst->getAddr(),
                                   dinst->getPC(),
                                   dinst,
                                   performed_spec_CB::create(this, dinst));
#else
    // printf("Resource::cacheDispatched::SPEC_LOAD_SCB_ALL::PerformedCB::sendSpecL1LoadREAD cache::dinst  %llu\n", dinst->getID());
    // printf("FULoad::Resource::Cachedispatched::dinst  %llu and clock cycle %llu \n", dinst->getID(), globalClock);
    dinst->set_load_scb_all();
    MemRequest::sendSpecReqDL1Read(firstLevelMemObj,
                                   dinst->has_stats(),
                                   dinst->getAddr(),
                                   dinst->getPC(),
                                   dinst,
                                   // performedCB::create(this, dinst));
                                   performedCB::create(this, dinst, dinst->getID()));
#endif
  } else {
    // printf("Resource::cacheDispatched::SAFE_LOAD_SCB_ALL::PerformedCB::sendsafeL1LoadREAD cache::dinst  %llu\n", dinst->getID());
    // printf("FULoad::Resource::Cachedispatched::dinst  %llu and clock cycle %llu \n", dinst->getID(), globalClock);
    dinst->set_load_scb_all();
    MemRequest::sendSafeReqDL1Read(firstLevelMemObj,
                                   dinst->has_stats(),
                                   dinst->getAddr(),
                                   dinst->getPC(),
                                   dinst,
                                   performedCB::create(this, dinst, dinst->getID()));
  }

  pref->exe(dinst);
}
/* }}} */

void FULoad::executed(Dinst* dinst) {
  /* executed {{{1 */

  // printf("FULoad::Resource::executed:: Entering executed() dinst  %llu ai clockcycle %llu\n", dinst->getID(), globalClock);
  if (dinst->isExecuted()) {
    // printf("FULoad::Resource::executed:: Already executed() so return from here!!! dinst  %llu\n", dinst->getID());
    return;
  }
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

  // printf("FULoad::Resource::executed:: Entering Cluster->executed() dinst  %llu and clock cycle %llu \n",
         // dinst->getID(),
         // globalClock);
  cluster->executed(dinst);
  // printf("FULoad::Resource::executed:: Leaving Resource::executed() dinst  %llu and clock cycle %llu \n",
         // dinst->getID(),
         // globalClock);
}
/* }}} */

bool FULoad::preretire(Dinst* dinst, [[maybe_unused]] bool flushing)
/* retire {{{1 */
{  // PNR: POINT OF NO RETURN:NO SPEC after this point: only safe instructions continue
  // printf("FULoad::Resource::Preretire:: Entering preretire()dinst  %llu\n", dinst->getID());
  bool done = dinst->isDispatched();
  // L1 req sent to cache(done ==1)
  if (!done) {
    // printf(
        // "FULoad::Resource::Preretire:: Leaving preretire() readreq !done + not sent to cache:: NoT dispatched to cache read req "
        // "!!! dinst  %llu\n",
        // dinst->getID());
    return false;
  }

#ifdef ENABLE_SCB_SPEC
  // printf("FULoad::Resource::Preretire::cache req will dispatched shortly for dinst  %llu\n", dinst->getID());
  if (dinst->is_spec()) {
    // printf("FULoad::Resource::Preretire::cache SPEC_LOAD will dispatched to L1 and then return to SPEC_BUFFER for dinst  %llu\n",
           // dinst->getID());
    if (dinst->is_present_in_scb()) {
      // printf("Resource::Preretire:: spec() + present_in_scb removing from scb and set_safe::dinst  %llu\n", dinst->getID());
      scb->remove_spec_load(dinst);
      dinst->reset_present_in_scb();
    }
    dinst->set_safe();
    // executed(dinst);
    // dinst->markPerformed();
    // printf("Resource::Preretire::Before sending in preretire() spec::sendSafeL1Write::dinst  %llu\n", dinst->getID());
    if (enableDcache && !dinst->isTransient()) {
      // printf("Resource::Preretire::sendSafeL1Write after PNR to Dcache::dinst  %llu\n", dinst->getID());
      if (scb->is_clean_disp(dinst)) {
        /*MemRequest::sendReqWrite(firstLevelMemObj,
                           dinst->has_stats(),
                           dinst->getAddr(),
                           dinst->getPC(),
                           performed_safe_write_CB::create(this, dinst));*/
        MemRequest::send_scb_clean_disp(firstLevelMemObj,
                                        dinst->has_stats(),
                                        dinst->getAddr(),
                                        dinst->getPC(),
                                        performed_safe_write_CB::create(this, dinst));
      } else {
        /*MemRequest::sendReqWrite(firstLevelMemObj,
                          dinst->has_stats(),
                          dinst->getAddr(),
                          dinst->getPC(),
                          performed_safe_write_CB::create(this, dinst));*/
        MemRequest::send_scb_dirty_disp(firstLevelMemObj,
                                        dinst->has_stats(),
                                        dinst->getAddr(),
                                        dinst->getPC(),
                                        performed_safe_write_CB::create(this, dinst));
      }
    }
  }
#endif
  if (!dinst->is_try_flush_transient()) {
#ifdef USE_PNR
    freeEntries++;
#endif

    pref->ret(dinst);
  }

  return true;
}
/* }}} */

bool FULoad::retire(Dinst* dinst, [[maybe_unused]] bool flushing)
/* retire {{{1 */
{
  // both spec->become safe+ safe from origin may come
  if (!dinst->isPerformed()) {
    return false;
  }

  if (!dinst->is_try_flush_transient()) {
    lsq->incFreeEntries();
#ifndef USE_PNR
    freeEntries++;
#endif

#ifdef MEM_TSO2
    if (DL1->Invalid(dinst->getAddr())) {
      tso2Replay.inc(dinst->has_stats());
      dinst->getGProc()->replay(dinst);
    }
#endif

    /*LSQFull ends here*/
    lsq->remove(dinst);

    // VTAGE->updateVtageTables() here ???? vtage validation

  }
  setStats(dinst);

  return true;
}
/* }}} */

bool FULoad::flushed(Dinst* dinst)
/* flushing {{{1 */

{
  (void)dinst;
  return true;
}
/* }}} */

bool FULoad::try_flushed(Dinst* dinst)
/* flushing {{{1 */
{
  // unresolved-- in not happening when try_flush
  dinst->mark_try_flush_transient();
  if (!dinst->isRetired() && dinst->isExecuted()) {
    freeEntries++;
    lsq->incFreeEntries();
    lsq->remove(dinst);
    dinst->clearRATEntry();
  } else if (dinst->isExecuting() || dinst->isIssued()) {
    /* possible lsq->resolved-- is not done here*/
    freeEntries++;
    lsq->incFreeEntries();
    lsq->remove(dinst);
    storeset->remove(dinst);
    dinst->clearRATEntry();
  } else if (dinst->isRenamed()) {
    freeEntries++;
    lsq->incFreeEntries();
    lsq->remove(dinst);
    storeset->remove(dinst);
    dinst->clearRATEntry();
  }

  return true;
}
/* }}} */

void FULoad::performed(Dinst* dinst) {
  /* memory operation was globally performed {{{1 */
  // printf("FULoad::Resource::Performed:: Entering Performed for dinst  %llu and clock cycle %llu \n", dinst->getID(), globalClock);
  // printf("Resource::performed::Entering performed  dinst  %llu\n", dinst->getID());
  dinst->markPerformed();
  if (!dinst->isExecuted()) {
    // printf("Resource::performed::executed in performed  dinst  %llu\n", dinst->getID());
    // printf("Resource::performed::Entering executed for instID %llu at @Clockcycle %llu\n", dinst->getID(), globalClock);

    executed(dinst);
    // printf("Resource::performed:: Leaving executed for instID %llu at @Clockcycle %llu\n", dinst->getID(), globalClock);
  }
  /*if(dinst->isRetired()){
    // printf("Resource::performed_Safe_write:: LOADDestroying  dinst  %ld\n", dinst->getID());
    dinst->destroy();
   } else {
    dinst->set_load_destroyed_retired();
    // printf("Resource::performed_Safe_write::NOT LOADDestroying  dinst  %ld\n", dinst->getID());
   */

  // printf("FULoad::Resource::Performed:: Leaving Performed for dinst  %llu and clock cycle %llu \n", dinst->getID(), globalClock);
}

/* }}} */

void FULoad::performed_spec(Dinst* dinst) {
  /* memory operation was globally performed {{{1 */

  /* if spec then put in the scb and send to core to execute;
     but donot perform;wait until PNR/preretire()*/
  // printf("Resource::performed_SPEC::Entering SPEC performed dinst  %llu\n", dinst->getID());
  // printf("FULoad::Resource::Performed_SPEC:: Entering Performed for dinst  %llu and clock cycle %llu \n",
         // dinst->getID(),
         // globalClock);
#ifdef ENABLE_SCB_SPEC
  // if(dinst->is_spec() || if(dinst->isTransient()) {
  if (dinst->is_spec()) {
    // printf("Resource::Performed_spec:: spec() + inserting in scb ::setting present_in_scb dinst  %llu\n", dinst->getID());
    Addr_t addr = dinst->getAddr();
    if (scb->can_accept_st(addr)) {
      // printf("Resource::FULOAD::performed_spec::can_accept_st::TRUE return can accept::  addr %llu", addr);
      scb->add_st(dinst);
      dinst->set_present_in_scb();
    } else {
      // printf("Resource::FULOAD::performed_spec::can_accept_st::FALSE return cannot accept::  addr %llu", addr);
    }
    if (!dinst->isExecuted()) {
      executed(dinst);
    }
    // printf("Resource::performed_spec::insert into scb + executed+ !perfomed yet(spec) dinst  %llu\n", dinst->getID());
  } else if (dinst->is_safe()) {
    /*already preretire() happened before performedspec comes */
    dinst->markPerformed();
    if (!dinst->isExecuted()) {
      executed(dinst);
      // printf("Resource::FULoad::performed_spec::issafe() in preretire()+ no scb +L1loadwrite send from preretire() dinst  %llu\n",
             // dinst->getID());
    }
  }
  if (dinst->isRetired()) {
    if (dinst->is_load_destroyed_performed_spec()) {
      // printf("Resource::performed_Spec::retired + performed_safe_write already done:: LOADDestroying  dinst  %llu\n",
             // dinst->getID());
      dinst->destroy();
    } else {
      dinst->set_load_destroyed_performed_safe_write();
      // printf("Resource::performed_Safe_write::retired + !performed_safe_write :: NOT done:: :NOT LOADDestroying  dinst  %llu\n",
             // dinst->getID());
    }
  } else {
    dinst->set_load_destroyed_performed_safe_write();
    dinst->set_load_destroyed_retired_spec();
    // printf("Resource::performed_Safe_write:: !Retired::NOT LOADDestroying  dinst  %llu\n", dinst->getID());
  }
#endif
  // printf("FULoad::Resource::Performed_SPEC:: Leaving Performed for dinst  %llu and clock cycle %llu \n",
         // dinst->getID(),
         // globalClock);
}

void FULoad::performed_safe_write(Dinst* dinst) {
  // printf("Resource::performed_Safe_write::Entering  performed_safe_write dinst  %llu\n", dinst->getID());
  // printf("FULoad::Resource::Performed_Safe_write:: Entering Performed for dinst  %llu and clock cycle %llu \n",
         // dinst->getID(),
         // globalClock);
  dinst->markPerformed();
  if (dinst->is_present_in_scb()) {
    scb->remove_spec_load(dinst);
    dinst->reset_present_in_scb();
  }
  // not in ooop::retire()::destroy
  if (dinst->isRetired()) {
    if (dinst->is_load_destroyed_performed_safe_write()) {
      /*already performed_spec happened; so can be destroyed*/
      // printf("Resource::performed_Safe_write:: LOADDestroying  dinst  %llu\n", dinst->getID());
      dinst->destroy();
    } else {
      /*setting destroyed to happen in performed_spec as performed_safe_write happens before*/
      dinst->set_load_destroyed_performed_spec();
      // printf("Resource::performed_Safe_write:: retired + !performed_spec ::not done::NOT LOADDestroying  dinst  %llu\n",
             // dinst->getID());
    }
  } else {
    dinst->set_load_destroyed_performed_spec();
    dinst->set_load_destroyed_retired_safe_write();
    // printf("Resource::performed_Safe_write:: !Retired::NOT LOADDestroying  dinst  %llu\n", dinst->getID());
  }
}

/***********************************************/

FUStore::FUStore(Opcode type, std::shared_ptr<Cluster> cls, std::shared_ptr<PortGeneric> aGen, LSQ* _lsq,
                 std::shared_ptr<StoreSet> ss, std::shared_ptr<Prefetcher> _pref, std::shared_ptr<Store_buffer> _scb, TimeDelta_t l,
                 std::shared_ptr<Gmemory_system> ms, int32_t size, int32_t id, const std::string& cad)
    /* constructor {{{1 */
    : MemResource(type, cls, aGen, _lsq, ss, _pref, _scb, l, ms, id, cad), freeEntries(size) {
  enableDcache = Config::get_bool("soc", "core", id, "caches");
  LSQlateAlloc = Config::get_bool("soc", "core", id, "stq_late_alloc");
}
/* }}} */

StallCause FUStore::canIssue(Dinst* dinst) {
  /* canIssue {{{1 */

  // dinst->set_scb(scb);
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

  // printf("FUStore::Resource::Can issue for dinst  %llu\n", dinst->getID());
  return NoStall;
}
/* }}} */

void FUStore::executing(Dinst* dinst) {
  /* executing {{{1 */

  // printf("FUStore::Resource::Executing for dinst  %llu\n", dinst->getID());
  if (!dinst->getInst()->isStoreAddress()) {
    Dinst* qdinst = lsq->executing(dinst);
    if (qdinst) {
      dinst->getGProc()->replay(qdinst);
      if (!dinst->getGProc()->is_nuking()) {
        stldViolations.inc(dinst->has_stats());
      }
      storeset->stldViolation(qdinst, dinst);
    }
  }

  cluster->executing(dinst);

  gen->schedule(dinst->has_stats(),
                dinst->getID(),
                dinst->isTransient(),
                [this, dinst](Time_t allocated_time) { do_store_execution(allocated_time, dinst); });
}
/* }}} */

void FUStore::do_store_execution(Time_t when, Dinst* dinst) {
  (void)when;  // Time not used - just consuming port slot

  if (dinst->getInst()->isStoreAddress()) {
    executed(dinst);
  } else {
    executed(dinst);
  }
}

void FUStore::executed(Dinst* dinst) {
  /* executed */
  // printf("FUStore::Resource::Executed for dinst  %llu\n", dinst->getID());

  if (dinst->getInst()->isStore()) {
    storeset->remove(dinst);
  }

  cluster->executed(dinst);
}

bool FUStore::preretire(Dinst* dinst, bool flushing) {
  /* retire {{{1 */
  // printf("FUStore::Resource::Entering preretire() for dinst  %llu\n", dinst->getID());

  if (!dinst->isExecuted()) {
    // printf("FUStore::Resource:: !dinst->executed .so leaving preretire() for dinst  %llu\n", dinst->getID());
    return false;
  }
  if (dinst->getInst()->isStoreAddress()) {
    return true;
  }
  if (flushing) {
    freeEntries++;
    performed(dinst);
    return true;
  }

#ifdef ENABLE_SCB_ALL
  if (!scb->can_accept_st(dinst->getAddr())) {
    // printf("FUStore::Resource:: scb !can_accept_st .so leaving preretire() for dinst  %llu\n", dinst->getID());
    return false;
  }
#endif

  if (firstLevelMemObj->isBusy(dinst->getAddr())) {
    // printf("FUStore:: memObj busy .so leaving preretire() for dinst  %llu\n", dinst->getID());
    return false;
  }

#ifdef ENABLE_SCB_ALL
  // Basic SCB is on
  if (!dinst->isTransient() || !dinst->is_spec()) {
    scb->add_st(dinst);
  }
  performed(dinst);
#else
  /*SCB is not  used here*/
  if (enableDcache && !dinst->isTransient() && !dinst->is_spec()) {
    // printf("FUStore::Resource::preretire() sendReqWrite for dinst  %llu\n", dinst->getID());
    MemRequest::sendReqWrite(firstLevelMemObj,
                             dinst->has_stats(),
                             dinst->getAddr(),
                             dinst->getPC(),
                             performedCB::create(this, dinst));
  }
#endif

  /*scb->add_st(dinst);

  if (enableDcache && !dinst->isTransient()) {
    MemRequest::sendReqWrite(firstLevelMemObj,
                             dinst->has_stats(),
                             dinst->getAddr(),
                             dinst->getPC(),
                             performedCB::create(this, dinst, dinst->getID()));
  } else {
    performed(dinst);
  }*/

  freeEntries++;
  // printf("FUStore::Resource::Leaving preretire() for dinst  %llu\n", dinst->getID());

  return true;
}
/* }}} */

bool FUStore::flushed(Dinst* dinst)
/* flushing {{{1 */
{
  (void)dinst;
  return true;
}
/* }}} */

bool FUStore::try_flushed(Dinst* dinst)
/* flushing {{{1 */
{
  freeEntries++;
  lsq->remove(dinst);
  lsq->incFreeEntries();
  storeset->remove(dinst);
  performed(dinst);
  return true;
}
/* }}} */
void FUStore::performed(Dinst* dinst) {
  /* memory operation was globally performed {{{1 */

  // printf("Resource::FUStore::performed::Entering  dinst  %llu\n", dinst->getID());
  if (dinst->is_write_scb_r()) {
    // printf("Resource::FUStore::performed:: Spec_write_scb_retire  dinst  %llu\n", dinst->getID());
    scb->set_clean_scb(dinst);
  }
  setStats(dinst);  // Not retire for stores
  if (!dinst->isTransient()) {
    // printf("Resource::FUStore::performed:: !transient so notdestroyed dinst  %llu\n", dinst->getID());
    I(!dinst->isPerformed());
  }
  if (dinst->isRetired()) {
    // printf("Resource::FUStore::performed:: Not retired so notdestroyed dinst  %llu\n", dinst->getID());
    dinst->destroy();
  }
  dinst->markPerformed();
  // printf("Resource::FUStore::performed::Leaving  markperformed() dinst  %llu\n", dinst->getID());
}
/* }}} */

bool FUStore::retire(Dinst* dinst, bool flushing) {
  // both spec->become safe+ safe from origin may come;; set_safe is not done so ispec() is ok
  (void)flushing;
  /* if (enableDcache && dinst->is_write_scb_r() && !dinst->isPerformed()&& !dinst-isTransient() && !dinst->is_spec()) {
   // printf("FUStore::Resource::preretire() sendReqWrite for dinst  %ld\n", dinst->getID());
   MemRequest::sendReqWrite(firstLevelMemObj,
                            dinst->has_stats(),
                            dinst->getAddr(),
                            dinst->getPC(),
                            performedCB::create(this, dinst));
   }
*/
  // printf("FUStore::Resource::Entering retire() for dinst  %llu\n", dinst->getID());
  if (dinst->getInst()->isStoreAddress()) {
    setStats(dinst);
    // printf("FUStore::Resource::Leaving retire() for dinst  %llu\n", dinst->getID());
    return true;
  }

  I(!dinst->isRetired());
  dinst->markRetired();

  dinst->clearRATEntry();  // Stores could have dependences for LL and atomics. Clear at retire

  lsq->remove(dinst);
  lsq->incFreeEntries();
  // printf("FUStore::Resource::Leaving retire() for dinst  %llu\n", dinst->getID());

  return true;
}
void FUStore::performed_spec(Dinst*) {}
void FUStore::performed_safe_write(Dinst*) {
  // scb->set_clean(dinst)
}

/***********************************************/

FUGeneric::FUGeneric(Opcode type, std::shared_ptr<Cluster> cls, std::shared_ptr<PortGeneric> aGen, TimeDelta_t l, uint32_t cpuid)
    /* constructor {{{1 */
    : Resource(type, cls, aGen, l, cpuid) {}
/* }}} */

StallCause FUGeneric::canIssue(Dinst* dinst) {
  (void)dinst;
  return NoStall;
}

void FUGeneric::executing(Dinst* dinst) {
  /* executing {{{1 */
  gen->schedule(dinst->has_stats(),
                dinst->getID(),
                dinst->isTransient(),
                [this, dinst](Time_t allocated_time) { do_generic_execution(allocated_time, dinst); });
}
/* }}} */

void FUGeneric::do_generic_execution(Time_t when, Dinst* dinst) {
  Time_t nlat = when + lat;
  cluster->executing(dinst);
  executedCB::scheduleAbs(nlat, this, dinst, dinst->getID());
}

void FUGeneric::executed(Dinst* dinst) {
  /* executed {{{1 */
  cluster->executed(dinst);
  dinst->markPerformed();
}
/* }}} */

bool FUGeneric::preretire(Dinst* dinst, bool flushing)
/* preretire {{{1 */
{
  (void)flushing;
  return dinst->isExecuted();
}
/* }}} */

bool FUGeneric::retire(Dinst* dinst, bool flushing)
/* retire {{{1 */
{
  (void)flushing;
  setStats(dinst);

  return true;
}
/* }}} */

bool FUGeneric::flushed(Dinst* dinst)
/* flushing {{{1 */
{
  (void)dinst;
  return true;
}
/* }}} */

bool FUGeneric::try_flushed(Dinst* dinst)
/* flushing {{{1 */
{
  (void)dinst;
  return true;
}
/* }}} */

void FUGeneric::performed(Dinst* dinst) {
  /* memory operation was globally performed {{{1 */
  dinst->markPerformed();
  I(0);  // It should be called only for memory operations
}
void FUGeneric::performed_spec(Dinst*) {}
void FUGeneric::performed_safe_write(Dinst*) {}
/* }}} */

/***********************************************/

FUBranch::FUBranch(Opcode type, std::shared_ptr<Cluster> cls, std::shared_ptr<PortGeneric> aGen, TimeDelta_t l, uint32_t cpuid,
                   int32_t mb, bool dom)
    /* constructor {{{1 */
    : Resource(type, cls, aGen, l, cpuid), freeBranches(mb), drainOnMiss(dom) {
  I(freeBranches > 0);

  auto cpu_section   = Config::get_string("soc", "core", cpuid);
  auto bpred_section = Config::get_array_string(cpu_section, "bpred", 0);
  bpred_delay        = Config::get_integer(bpred_section, "delay", 1);
}
/* }}} */

StallCause FUBranch::canIssue(Dinst* dinst) {
  /* canIssue {{{1 */

  I(dinst->getInst()->isControl());
  if (freeBranches == 0) {
    return OutsBranchesStall;
  }
  // take out a branch from the branchpool
  freeBranches--;

  return NoStall;
}
/* }}} */

void FUBranch::executing(Dinst* dinst) {
  /* executing {{{1 */
  gen->schedule(dinst->has_stats(),
                dinst->getID(),
                dinst->isTransient(),
                [this, dinst](Time_t allocated_time) { do_branch_execution(allocated_time, dinst); });
}
/* }}} */

void FUBranch::do_branch_execution(Time_t when, Dinst* dinst) {
  cluster->executing(dinst);
  executedCB::scheduleAbs(when + lat + bpred_delay, this, dinst, dinst->getID());
}

void FUBranch::executed(Dinst* dinst) {
  /* executed {{{1 */
  cluster->executed(dinst);
  dinst->markPerformed();

  if (!drainOnMiss && dinst->isBranchMiss()) {
    // printf("Resource::FUBranch::executed::unBlockFetch dinstID %llu at clock cycle %llu\n", dinst->getID(), globalClock);
    (dinst->getFetchEngine())->unBlockFetch(dinst, dinst->getFetchTime());
  }

  // NOTE: assuming that once the branch is executed the entry can be recycled
  // recycle the branches to branch pool as the branch inst is executed
  freeBranches++;
}
/* }}} */

bool FUBranch::preretire(Dinst* dinst, bool flushing)
/* preretire {{{1 */
{
  (void)flushing;
  if (drainOnMiss && dinst->isExecuted() && dinst->isBranchMiss()) {
    // printf("Resource::FUBranch::preretire::unBlockFetch dinstID %llu at clock cycle %llu\n", dinst->getID(), globalClock);
    (dinst->getFetchEngine())->unBlockFetch(dinst, dinst->getFetchTime());
  }
  return dinst->isExecuted();
}
/* }}} */

bool FUBranch::retire(Dinst* dinst, [[maybe_unused]] bool flushing)
/* retire {{{1 */
{
  setStats(dinst);
  return true;
}
/* }}} */
bool FUBranch::flushed(Dinst* dinst)
/* flushing {{{1 */
{
  (void)dinst;
  return true;
}
/* }}} */

bool FUBranch::try_flushed(Dinst* dinst)
/* flushing {{{1 */
{
  freeBranches++;
  (void)dinst;
  return true;
}
/* }}} */

void FUBranch::performed(Dinst* dinst) {
  /* memory operation was globally performed {{{1 */
  dinst->markPerformed();
  I(0);  // It should be called only for memory operations
}

void FUBranch::performed_spec(Dinst*) {}

void FUBranch::performed_safe_write(Dinst*) {}
/* }}} */

/***********************************************/

FURALU::FURALU(Opcode type, std::shared_ptr<Cluster> cls, std::shared_ptr<PortGeneric> aGen, TimeDelta_t l, int32_t id)
    /* constructor {{{1 */
    : Resource(type, cls, aGen, l, id)
    , dmemoryBarrier(fmt::format("P({})_{}_dmemoryBarrier", id, cls->getName()))
    , imemoryBarrier(fmt::format("P({})_{}_imemoryBarrier", id, cls->getName())) {
  blockUntil = 0;
}
/* }}} */

StallCause FURALU::canIssue(Dinst* dinst)
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

void FURALU::executing(Dinst* dinst) {
  /*executing {{{1 */
  gen->schedule(dinst->has_stats(),
                dinst->getID(),
                dinst->isTransient(),
                [this, dinst](Time_t allocated_time) { do_ralu_execution(allocated_time, dinst); });
}
/* }}} */

void FURALU::do_ralu_execution(Time_t when, Dinst* dinst) {
  // printf("Resource::FURALU::do_alu_execution:: Entering  Inst %llu at clock cycle %llu\n", dinst->getID(), globalClock);
  cluster->executing(dinst);
  // printf("Resource::FURALU::do_alu_execution:: When is %llu and lat is %d for Inst %llu at clock cycle %llu\n",
         // when,
         // lat,
         // dinst->getID(),
         // globalClock);
  executedCB::scheduleAbs(when + lat, this, dinst, dinst->getID());
  // printf("Resource::FURALU::do_alu_execution:: Leaving  Inst %llu at clock cycle %llu\n", dinst->getID(), globalClock);
}

void FURALU::executed(Dinst* dinst)
/* executed {{{1 */
{
  // printf("Resource::FURALU::Executed:: Entering  Inst %llu at clock cycle %llu\n", dinst->getID(), globalClock);
  // bool pend = dinst->hasPending();
  cluster->executed(dinst);
  dinst->markPerformed();
  // printf("Resource::FURALU::do_alu_executed:: Leaving  Inst %llu at clock cycle %llu\n", dinst->getID(), globalClock);
}
/* }}} */

bool FURALU::preretire(Dinst* dinst, [[maybe_unused]] bool flushing)
/* preretire ensures the inst is  executed {{{1 */
{
  // if(dinst->getCluster()->get_reg_pool() >= dinst->getCluster()->get_nregs()-2) {
  // return false;
  // }
  // if( dinst->getCluster()->get_window_size() == dinst->getCluster()->get_window_maxsize()){
  //  return false;
  // }
  return dinst->isExecuted();
}
/* }}} */

bool FURALU::retire(Dinst* dinst, [[maybe_unused]] bool flushing)
/* retire always true{{{1 */
{
  // if(dinst->getCluster()->get_reg_pool() >= dinst->getCluster()->get_nregs()-2) {
  // return false;
  // }
  // if( dinst->getCluster()->get_window_size() == dinst->getCluster()->get_window_maxsize()){
  //  return false;
  //}
  if (dinst->isTransient()) {
    dinst->mark_retired();
  }
  if (!dinst->isTransient()) {
    setStats(dinst);
  }

  return true;
}
/* }}} */

bool FURALU::flushed(Dinst* dinst)
/* flushing {{{1 */
{
  // cluster->flushed(dinst);
  if (!dinst->isExecuted()) {
    dinst->markExecutedTransient();
    dinst->clearRATEntry();
    Tracer::stage(dinst, "TR");
  }
  return true;
}
/* }}} */

bool FURALU::try_flushed(Dinst* dinst)
/* flushing {{{1 */
{
  (void)dinst;
  return true;
}
/* }}} */

void FURALU::performed(Dinst* dinst)
/* memory operation was globally performed {{{1 */
{
  dinst->markPerformed();
  I(0);  // It should be called only for memory operations
}
/* }}} */
void FURALU::performed_spec(Dinst*) {}
void FURALU::performed_safe_write(Dinst*) {}
