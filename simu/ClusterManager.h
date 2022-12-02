// See LICENSE for details.

#pragma once

#include "ClusterScheduler.h"
#include "DepWindow.h"
#include "dinst.hpp"
#include "iassert.hpp"

class Resource;
class GMemorySystem;
class GProcessor;

class ClusterManager {
private:
  ClusterScheduler *scheduler;

protected:
public:
  ClusterManager(GMemorySystem *ms, uint32_t cpuid, GProcessor *gproc);

  Resource *getResource(Dinst *dinst) const { return scheduler->getResource(dinst); }
};
