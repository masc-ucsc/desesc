// See LICENSE for details.

#pragma once

#include "dinst.hpp"
#include "iassert.hpp"

#include "ClusterScheduler.h"
#include "DepWindow.h"
#include "GStats.h"

class Resource;
class GMemorySystem;
class GProcessor;

class ClusterManager {
private:
  ClusterScheduler *scheduler;

protected:
public:
  ClusterManager(GMemorySystem *ms, uint32_t cpuid, GProcessor *gproc);

  Resource *getResource(DInst *dinst) const {
    return scheduler->getResource(dinst);
  }
};

