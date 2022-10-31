// See LICENSE for details.

#pragma once

#include <map>
#include <set>
#include <vector>

#include "dinst.hpp"

#include "GStats.h"
#include "estl.h"

class LSQ {
private:
protected:
  int32_t freeEntries;
  int32_t unresolved;

  LSQ(int32_t size) {
    freeEntries = size;
    unresolved  = 0;
  }

  virtual ~LSQ() {
  }

public:
  virtual bool   insert(Dinst *dinst)    = 0;
  virtual Dinst *executing(Dinst *dinst) = 0;
  virtual void   remove(Dinst *dinst)    = 0;

  void incFreeEntries() {
    freeEntries++;
  }
  void decFreeEntries() {
    unresolved++;
    freeEntries--;
  }
  bool hasFreeEntries() const {
    return freeEntries > 0;
  }
  bool hasPendingResolution() const {
    return unresolved > 0;
  }
};

class LSQFull : public LSQ {
private:
  typedef HASH_MULTIMAP<AddrType, Dinst *> AddrDinstQMap;

  GStatsCntr    stldForwarding;
  AddrDinstQMap instMap;

  static AddrType calcWord(const Dinst *dinst) {
    return (dinst->getAddr()) >> 3;
  }

public:
  LSQFull(const int32_t id, int32_t size);
  ~LSQFull() {
  }

  bool   insert(Dinst *dinst);
  Dinst *executing(Dinst *dinst);
  void   remove(Dinst *dinst);
};

class LSQNone : public LSQ {
private:
  Dinst *addrTable[128];

  int getEntry(AddrType addr) const {
    return ((addr >> 1) ^ (addr >> 17)) & 127;
  }

public:
  LSQNone(const int32_t id, int32_t size);
  ~LSQNone() {
  }

  bool   insert(Dinst *dinst);
  Dinst *executing(Dinst *dinst);
  void   remove(Dinst *dinst);
};

class LSQVPC : public LSQ {
private:
  std::multimap<AddrType, Dinst *> instMap;

  GStatsCntr LSQVPC_replays;

  static AddrType calcWord(const Dinst *dinst) {
    return (dinst->getAddr()) >> 2;
  }

public:
  LSQVPC(int32_t size);
  ~LSQVPC() {
  }

  bool     insert(Dinst *dinst);
  Dinst *  executing(Dinst *dinst);
  void     remove(Dinst *dinst);
  AddrType replayCheck(Dinst *dinst);
};

