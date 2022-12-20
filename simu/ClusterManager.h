// See LICENSE for details.

#pragma once

#include "ClusterScheduler.h"
#include "DepWindow.h"
#include "dinst.hpp"
#include "iassert.hpp"
#include "GMemorySystem.h"

class Resource;
class GProcessor;

class ClusterManager {
private:
  ClusterScheduler *scheduler;

protected:
public:
  ClusterManager(std::shared_ptr<GMemorySystem> gms, uint32_t cpuid, GProcessor *gproc);

  Resource *getResource(Dinst *dinst) const { return scheduler->getResource(dinst); }
};
