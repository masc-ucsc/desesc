// See LICENSE for details.

#include "mshr.hpp"

#include "config.hpp"
#include "fmt/format.h"
#include "memory_system.hpp"
#include "memrequest.hpp"
#include "snippets.hpp"

MSHR::MSHR(const std::string &n, int32_t size, int16_t lineSize, int16_t nsub)
    : name(n)
    , Log2LineSize(log2i(lineSize))
    , nEntries(size)
    , nSubEntries(nsub)
    , avgUse(fmt::format("{}_MSHR_avgUse", n))
    , avgSubUse(fmt::format("{}_MSHR_avgSubUse", n))
    , nStallConflict(fmt::format("{}_MSHR:nStallConflict", name))
    , MSHRSize(roundUpPower2(size) * 4)
    , MSHRMask(MSHRSize - 1) {
  I(size > 0 && size < 1024 * 32 * 32);

  nFreeEntries = size;

  I(lineSize >= 0 && Log2LineSize < (8 * sizeof(Addr_t) - 1));

  entry.resize(MSHRSize);

  for (int32_t i = 0; i < MSHRSize; i++) {
    entry[i].nUse = 0;
    I(entry[i].cc.empty());
  }
}

bool MSHR::canAccept(Addr_t addr) const {
  if (nFreeEntries <= 0) {
    return false;
  }

  uint32_t pos = calcEntry(addr);
  if (entry[pos].nUse >= nSubEntries) {
    return false;
  }

  return true;
}

bool MSHR::canIssue(Addr_t addr) const {
  uint32_t pos = calcEntry(addr);
  if (entry[pos].nUse) {
    return false;
  }

  I(entry[pos].cc.empty());

  return true;
}

void MSHR::addEntry(Addr_t addr, CallbackBase *c, MemRequest *mreq) {
  I(mreq->isRetrying());
  I(nFreeEntries <= nEntries);
  nFreeEntries--;  // it can go negative because invalidate and writeback requests

  avgUse.sample(nEntries - nFreeEntries, mreq->has_stats());

  uint32_t pos = calcEntry(addr);

  I(c);
  entry[pos].cc.add(c);

  I(nFreeEntries >= 0);

  I(entry[pos].nUse);
  entry[pos].nUse++;
  avgSubUse.sample(entry[pos].nUse, mreq->has_stats());

  nStallConflict.inc();

#ifndef NDEBUG
  I(!entry[pos].pending_mreq.empty());
  entry[pos].pending_mreq.push_back(mreq);
#endif
}

void MSHR::blockEntry(Addr_t addr, MemRequest *mreq) {
  I(!mreq->isRetrying());
  I(nFreeEntries <= nEntries);
  nFreeEntries--;  // it can go negative because invalidate and writeback requests

  avgUse.sample(nEntries - nFreeEntries, mreq->has_stats());

  uint32_t pos = calcEntry(addr);
  I(nFreeEntries >= 0);

  I(entry[pos].nUse == 0);
  entry[pos].nUse++;
  avgSubUse.sample(entry[pos].nUse, mreq->has_stats());

#ifndef NDEBUG
  I(entry[pos].pending_mreq.empty());
  entry[pos].pending_mreq.push_back(mreq);
  entry[pos].block_mreq = mreq;
#endif
}

bool MSHR::retire(Addr_t addr, MemRequest *mreq) {
  I(mreq);
  uint32_t pos = calcEntry(addr);
  I(entry[pos].nUse);
#ifndef NDEBUG
  I(!entry[pos].pending_mreq.empty());
  I(entry[pos].pending_mreq.front() == mreq);
  entry[pos].pending_mreq.pop_front();
  if (!entry[pos].pending_mreq.empty()) {
    MemRequest *mreq2 = entry[pos].pending_mreq.front();
    if (mreq2 != entry[pos].block_mreq) {
      I(mreq2->isRetrying());
    } else {
      I(!mreq2->isRetrying());
    }
  }
  if (entry[pos].block_mreq) {
    I(mreq == entry[pos].block_mreq);
    entry[pos].block_mreq = 0;
  }
#endif

  nFreeEntries++;
  I(nFreeEntries <= nEntries);

  I(entry[pos].nUse);
  entry[pos].nUse--;

  I(nFreeEntries >= 0);

  GI(entry[pos].nUse == 0, entry[pos].cc.empty());

  if (!entry[pos].cc.empty()) {
    entry[pos].cc.callNext();
    return true;
  }

  return false;
}

void MSHR::dump() const {
  fmt::print("MSHR[{}]", name);
  for (int i = 0; i < MSHRSize; i++) {
    if (entry[i].nUse) {
      fmt::print(" [{}].nUse={}", i, entry[i].nUse);
    }
    GI(entry[i].nUse == 0, entry[i].cc.empty());
    // GI(entry[i].cc.empty(), entry[i].nUse==0);
  }
  fmt::print("\n");
}
