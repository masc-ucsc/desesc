// See LICENSE for details.

#include "storeset.hpp"

#include "config.hpp"

StoreSet::StoreSet(const int32_t id)
    /* constructor {{{1 */
    : StoreSetSize(Config::get_power2("soc", "core", id, "storeset_size", 128))
#ifdef STORESET_CLEARING
    , clearStoreSetsTimerCB(this)
#endif
{

  SSIT.resize(StoreSetSize);
  LFST.resize(StoreSetSize, nullptr);
  clear_SSIT();
  clear_LFST();

#ifdef STORESET_CLEARING
  clear_interval = CLR_INTRVL;
  Time_t when    = clear_interval + globalClock;
  if (globalClock && when >= (globalClock * 2)) {
    when = globalClock * 2 - 1;  // To avoid assertion about possible bug. Long enough anyway
  }
  clearStoreSetsTimerCB.scheduleAbs(when);
#endif
}
/* }}} */

StoreSet::~StoreSet() {}

SSID_t StoreSet::create_id() {
  static SSID_t rnd = 0;
  SSID_t        SSID;
#if 1
  if (rnd < StoreSetSize) {
    SSID = rnd;
    rnd++;
  } else {
    rnd  = 0;
    SSID = rnd;
  }
#else
  SSID = (PC % 32671) & (StoreSetSize - 1);
  // rnd += 1021; // prime number
#endif

  return SSID;
}
SSID_t StoreSet::create_set(uint64_t PC)
/* create_set {{{1 */
{
  SSID_t SSID = create_id();

#ifndef NDEBUG
  SSID_t oldSSID = get_SSID(PC);
#endif
  // I(LFST[SSID]==0);

  set_SSID(PC, SSID);
  LFST[SSID] = nullptr;

#ifndef NDEBUG
  I(SSID < StoreSetSize);
  if (isValidSSID(oldSSID)) {
    LFST[oldSSID] = nullptr;
  };  // debug only
#endif

  return SSID;
}
/* }}} */
#ifdef STORESET_MERGING
void StoreSet::merge_sets(Dinst *m_dinst, Dinst *d_dinst)
/* merge two loads into the src LD's set {{{1 */
{
  uint64_t merge_this_set_pc   = m_dinst->getPC();  // <<1 + m_dinst->getUopOffset();
  uint64_t destroy_this_set_pc = d_dinst->getPC();  // <<1 + d_dinst->getUopOffset();
  SSID_t   merge_SSID          = get_SSID(merge_this_set_pc);
  if (!isValidSSID(merge_SSID)) {
    merge_SSID = create_set(merge_this_set_pc);
  }

  set_SSID(destroy_this_set_pc, merge_SSID);
}
/* }}} */
#endif
void StoreSet::clear_SSIT()
/* Clear all the SSIT entries {{{1 */
{
  std::fill(SSIT.begin(), SSIT.end(), -1);
}
/* }}} */

void StoreSet::clear_LFST(void)
/* clear all the LFST entries {{{1 */
{
  std::fill(LFST.begin(), LFST.end(), nullptr);
}
/* }}} */

#ifdef STORESET_CLEARING
void StoreSet::clearStoreSetsTimer()
/* periodic cleanup of the LSFT and SSIT {{{1 */
{
  clear_SSIT();
  clear_LFST();
  Time_t when = clear_interval + globalClock;
  if (when >= (globalClock * 2)) {
    when = globalClock * 2 - 1;  // To avoid assertion about possible bug. Long enough anyway
  }
  clearStoreSetsTimerCB.scheduleAbs(when);
}
/* }}} */
#endif

bool StoreSet::insert(Dinst *dinst)
/* insert a store/load in the store set {{{1 */
{
  uint64_t inst_pc   = dinst->getPC();  // <<1+dinst->getUopOffset();
  SSID_t   inst_SSID = get_SSID(inst_pc);

  if (!isValidSSID(inst_SSID)) {
    dinst->setSSID(-1);
    return true;  // instruction does not belong to an existing set.
  }

  const Instruction *inst = dinst->getInst();

  if (inst->isStoreAddress()) {
    printf("Store Address passed to StoreSet insert. exit\n");
    exit(1);
  }
  I(!dinst->isExecuted());
  dinst->setSSID(inst_SSID);

  Dinst *lfs_dinst = get_LFS(inst_SSID);
  set_LFS(inst_SSID,
          dinst);  // make this instruction the Last Fetched Store(Should be renamed to instruction since loads are included).

  if (lfs_dinst == 0) {
    return true;
  }

  I(!lfs_dinst->isExecuted());
  I(!dinst->isExecuted());
  lfs_dinst->addSrc3(dinst);

  return true;
}
/* }}} */

void StoreSet::remove(Dinst *dinst)
/* remove a store from store sets {{{1 */
{
  I(!dinst->getInst()->isStoreAddress());

  SSID_t inst_SSID = dinst->getSSID();

  if (!isValidSSID(inst_SSID)) {
    return;
  }

  Dinst *lfs_dinst = get_LFS(inst_SSID);

  if (dinst == lfs_dinst) {
    I(isValidSSID(inst_SSID));
    set_LFS(inst_SSID, 0);
  }
}
/* }}} */

void StoreSet::stldViolation(Dinst *ld_dinst, uint64_t st_pc)
/* add a new st/ld violation {{{1 */
{
  return;  // FIXME: no store set
  I(ld_dinst->getInst()->isLoad());

  uint64_t ld_pc   = ld_dinst->getPC();
  SSID_t   ld_SSID = ld_dinst->getSSID();
  if (!isValidSSID(ld_SSID)) {
    ld_SSID = create_set(ld_pc);
  }
  set_SSID(st_pc, ld_SSID);
}
/* }}} */

void StoreSet::stldViolation(Dinst *ld_dinst, Dinst *st_dinst)
/* add a new st/ld violation {{{1 */
{
  stldViolation(ld_dinst, st_dinst->getPC());
}
/* }}} */

void StoreSet::stldViolation_withmerge(Dinst *ld_dinst, Dinst *st_dinst)
/* add a new st/ld violation {{{1 */
{
  I(st_dinst->getInst()->isStore());
  I(ld_dinst->getInst()->isLoad());

  uint64_t ld_pc   = ld_dinst->getPC();    // <<1 + ld_dinst->getUopOffset();
  uint64_t st_pc   = st_dinst->getPC();    // <<1 + st_dinst->getUopOffset();
  SSID_t   ld_SSID = ld_dinst->getSSID();  // get_SSID(ld_pc);
  SSID_t   st_SSID = st_dinst->getSSID();  // get_SSID(ld_pc);
  if (!isValidSSID(ld_SSID)) {
    ld_SSID = create_set(ld_pc);
  }
  if (isValidSSID(st_SSID)) {
    if (st_SSID != ld_SSID) {
      for (int i = 0; i < StoreSetSize; i++) {  // iterate SSIT, move all PCs from old set to THIS LD's set.
        if (SSIT[i] == st_SSID) {
          SSIT[i] = ld_SSID;
        }
      }
      LFST[st_SSID] = nullptr;  // Wipe out any pending LFST for the destroyed set.
    }
  }
  set_SSID(st_pc, ld_SSID);
}
/* }}} */

SSID_t StoreSet::mergeset(SSID_t id1, SSID_t id2)
/* add a new st/ld violation {{{1 */
{
  if (id1 == id2) {
    return id1;
  }

  // SSID_t newid = create_id();
  SSID_t newid;
  if (id1 < id2) {
    newid = id1;
  } else {
    newid = id2;
  }

  I(id1 != -1);
  I(id2 != -1);

  set_LFS(id1, 0);
  set_LFS(id2, 0);

  for (int i = 0; i < StoreSetSize; i++) {  // iterate SSIT, move all PCs from old set to THIS LD's set.
    if (SSIT[i] == id1 || SSIT[i] == id2) {
      SSIT[i] = newid;
    }
  }

  return newid;
}
/* }}} */

void StoreSet::VPC_misspredict(Dinst *ld_dinst, uint64_t st_pc)
/* add a new st/ld violation {{{1 */
{
  I(st_pc);
  I(ld_dinst->getInst()->isLoad());
  uint64_t ld_pc   = ld_dinst->getPC();    // <<1 + ld_dinst->getUopOffset();
  SSID_t   ld_SSID = ld_dinst->getSSID();  // get_SSID(ld_pc);
  if (!isValidSSID(ld_SSID)) {
    ld_SSID = create_set(ld_pc);
  }
  set_SSID(st_pc, ld_SSID);
}
/* }}} */

void StoreSet::assign_SSID(Dinst *dinst, SSID_t target_SSID) {
  /* force this dinst to join the specified set (for merging) {{{1 */
  SSID_t   inst_SSID = dinst->getSSID();
  uint64_t inst_pc   = dinst->getPC();
  if (isValidSSID(inst_SSID)) {
    Dinst *lfs_dinst = get_LFS(inst_SSID);
    if (lfs_dinst != nullptr) {
      // if(dinst->getID() == lfs_dinst->getID()){ //is this store or load the most recent from the set
      // if(dinst->getpersistentID() == lfs_dinst->getpersistentID()){ //is this store or load the most recent from the set
      if ((lfs_dinst == nullptr) || (lfs_dinst == dinst)) {
        if (inst_pc == lfs_dinst->getPC()) {
          set_LFS(inst_SSID, 0);  // remove self from LFS to prevent a deadlock after leaving this set
        }
      }
    }
  }
  set_SSID(inst_pc, target_SSID);
  dinst->setSSID(target_SSID);
}
/* }}} */
