// See LICENSE for details.

#pragma once

#include <limits.h>
#include <vector>

#include "iassert.hpp"
#include "instruction.hpp"

#include "estl.h"
#include "DepWindow.h"
#include "GStats.h"

class Resource;
class GMemorySystem;

class Cluster {
private:
  void buildUnit(const char *clusterName, uint32_t pos, GMemorySystem *ms, Cluster *cluster, uint32_t cpuid, InstOpcode type,
                 GProcessor *gproc);

protected:
  DepWindow window;

  const int32_t MaxWinSize;
  int32_t       windowSize;

  int32_t nready;

  GStatsAvg  winNotUsed;
  GStatsCntr rdRegPool;
  GStatsCntr wrRegPool;

  Resource *res[iMAX];

  int32_t nRegs;
  int32_t regPool;
  bool    lateAlloc;

  uint32_t cpuid;

  char *name;

protected:
  void delEntry() {
    windowSize++;
    I(windowSize <= MaxWinSize);
  }
  void newEntry() {
    windowSize--;
    I(windowSize >= 0);
  }

  virtual ~Cluster();
  Cluster(const char *clusterName, uint32_t pos, uint32_t cpuid);

public:
  void select(DInst *dinst);

  virtual void executing(DInst *dinst)           = 0;
  virtual void executed(DInst *dinst)            = 0;
  virtual bool retire(DInst *dinst, bool replay) = 0;

  static Cluster *create(const char *clusterName, uint32_t pos, GMemorySystem *ms, uint32_t cpuid, GProcessor *gproc);

  Resource *getResource(InstOpcode type) const {
    I(type < iMAX);
    return res[type];
  }

  const char *getName() const {
    return name;
  }

  StallCause canIssue(DInst *dinst) const;
  void       addInst(DInst *dinst);

  int32_t getAvailSpace() const {
    if(regPool < windowSize)
      return regPool;
    return windowSize;
  }
  int32_t getNReady() const {
    return nready;
  }
};

class ExecutingCluster : public Cluster {
  // This is SCOORE style. The instruction is removed from the queue at dispatch time
public:
  virtual ~ExecutingCluster() {
  }

  ExecutingCluster(const char *clusterName, uint32_t pos, uint32_t cpuid)
      : Cluster(clusterName, pos, cpuid) {
  }

  void executing(DInst *dinst);
  void executed(DInst *dinst);
  bool retire(DInst *dinst, bool replay);
};

class ExecutedCluster : public Cluster {
public:
  virtual ~ExecutedCluster() {
  }

  ExecutedCluster(const char *clusterName, uint32_t pos, uint32_t cpuid)
      : Cluster(clusterName, pos, cpuid) {
  }

  void executing(DInst *dinst);
  void executed(DInst *dinst);
  bool retire(DInst *dinst, bool replay);
};

class RetiredCluster : public Cluster {
public:
  virtual ~RetiredCluster() {
  }
  RetiredCluster(const char *clusterName, uint32_t pos, uint32_t cpuid)
      : Cluster(clusterName, pos, cpuid) {
  }

  void executing(DInst *dinst);
  void executed(DInst *dinst);
  bool retire(DInst *dinst, bool replay);
};

