// See LICENSE for details.

#include "lsq.hpp"

#include "config.hpp"
#include "fmt/format.h"
#include "gprocessor.hpp"

LSQFull::LSQFull(Hartid_t hid, int32_t size)
    /* constructor {{{1 */
    : LSQ(size), stldForwarding(fmt::format("P({}):stldForwarding", hid)) {}
/* }}} */

bool LSQFull::insert(Dinst *dinst)
/* Insert dinst in LSQ (in-order) {{{1 */
{
  I(dinst->getAddr());
  instMap.insert(std::pair<Addr_t, Dinst *>(calcWord(dinst), dinst));

  return true;
}
/* }}} */

Dinst *LSQFull::executing(Dinst *dinst)
/* dinst got executed (out-of-order) {{{1 */
{
  I(dinst->getAddr());
  //I(!dinst->isTransient());

  Addr_t tag = calcWord(dinst);

  const Instruction *inst   = dinst->getInst();
  Dinst             *faulty = 0;

#if 0
  AddrDinstQMap::const_iterator instIt = instMap.begin();
  I(instIt != instMap.end());

  I(!dinst->isExecuted());

  while(instIt != instMap.end()) {
    if (instIt->first != tag){
      instIt++;
      continue;
    }
#endif
  std::pair<AddrDinstQMap::iterator, AddrDinstQMap::iterator> ret;
  ret = instMap.equal_range(tag);
  for (AddrDinstQMap::iterator instIt = ret.first; instIt != ret.second; ++instIt) {
    I(instIt->first == tag);

    // inst->dump("Executed");
    Dinst *qdinst = instIt->second;
    if (qdinst == dinst) {
      continue;
    }

    const Instruction *qinst = qdinst->getInst();

    // bool beforeInst = qdinst->getID() < dinst->getID();
    bool oooExecuted = qdinst->getID() > dinst->getID();
    if (oooExecuted) {
      if (qdinst->isExecuted() && qdinst->getPC() != dinst->getPC()) {
        if (inst->isStore() && qinst->isLoad()) {
          if (faulty == 0) {
            faulty = qdinst;
          } else if (faulty->getID() < qdinst->getID()) {
            faulty = qdinst;
          }
        }
      }
    } else {
      if (!dinst->isLoadForwarded() && inst->isLoad() && qinst->isStore() && qdinst->isExecuted()) {
        dinst->setLoadForwarded();
        stldForwarding.inc(dinst->has_stats());
      }
    }
  }

  unresolved--;
  I(!dinst->isExecuted());  // first clear, then mark executed
  return faulty;
}
/* }}} */

void LSQFull::remove(Dinst *dinst)
/* Remove from the LSQ {{{1 (in-order) */
{
  I(dinst->getAddr());

  // const Instruction *inst = dinst->getInst();

  std::pair<AddrDinstQMap::iterator, AddrDinstQMap::iterator> rangeIt;
  // rangeIt = instMap.equal_range(calcWord(dinst));
  AddrDinstQMap::iterator instIt = instMap.begin();

  // for(AddrDinstQMap::iterator it = rangeIt.first; it != rangeIt.second ; it++) {
  while (instIt != instMap.end()) {
    if (instIt->second == dinst) {
      instMap.erase(instIt);
      return;
    }
    instIt++;
  }
}
/* }}} */

LSQNone::LSQNone(Hartid_t hid, int32_t size)
    /* constructor {{{1 */
    : LSQ(size) {
  (void)hid;

  for (auto &e : addrTable) {
    e = 0;
  }
}
/* }}} */

bool LSQNone::insert(Dinst *dinst)
/* Insert dinst in LSQ (in-order) {{{1 */
{
  int i = getEntry(dinst->getAddr());
  if (addrTable[i]) {
    return false;
  }

  addrTable[i] = dinst;

  return true;
}
/* }}} */

Dinst *LSQNone::executing(Dinst *dinst)
/* dinst got executed (out-of-order) {{{1 */
{
  int i = getEntry(dinst->getAddr());
  I(addrTable[i] == dinst);
  addrTable[i] = 0;

  unresolved--;
  return 0;
}
/* }}} */

void LSQNone::remove(Dinst *dinst)
/* Remove from the LSQ {{{1 (in-order) */
{
  (void)dinst;
}
/* }}} */

LSQVPC::LSQVPC(int32_t size)
    /* constructor {{{1 */
    : LSQ(size), LSQVPC_replays("LSQVPC_replays") {}
/* }}} */

bool LSQVPC::insert(Dinst *dinst)
/* Insert dinst in LSQ (in-order) {{{1 */
{
  I(dinst->getAddr());
  instMap.insert(std::pair<Addr_t, Dinst *>(calcWord(dinst), dinst));

  return true;
}
/* }}} */

Dinst *LSQVPC::executing(Dinst *dinst) {
  (void)dinst;
  I(0);
  unresolved--;
  return 0;
}

Addr_t LSQVPC::replayCheck(Dinst *dinst)  // return non-zero if replay needed
/* dinst got executed (out-of-order) {{{1 */
{
  Addr_t                                   tag = calcWord(dinst);
  std::multimap<Addr_t, Dinst *>::iterator instIt;
  // instIt = instMap.begin();
  instIt = instMap.find(tag);
  // Addr_t storefound = 0;
  // while(instIt != instMap.end()){
  while (instIt->first == tag) {
    if (instIt->first != tag) {
      instIt++;
      continue;
    }
    if (instIt->second->getID() < dinst->getID()) {
      if (instIt->second->getAddr() == dinst->getAddr()) {
        LSQVPC_replays.inc(dinst->has_stats());
        return 1;
      }
    }
    // storefound = instIt->second->getPC();
    // if(instIt->second->getData()==dinst->getData()){
    //  return 0; //no replay needed
    //}
    instIt++;
  }
  return 0;
  // return storefound;
}
/* }}} */

void LSQVPC::remove(Dinst *dinst)
/* Remove from the LSQ {{{1 (in-order) */
{
  I(dinst->getAddr());
  std::multimap<Addr_t, Dinst *>::iterator instIt;
  // instIt = instMap.begin();
  Addr_t tag = calcWord(dinst);
  instIt     = instMap.find(tag);
  // while(instIt != instMap.end()){
  while (instIt->first == tag) {
    if (instIt->second == dinst) {
      instMap.erase(instIt);
      return;
    }
    instIt++;
    if (instIt == instMap.end()) {
      return;
    }
  }
}
