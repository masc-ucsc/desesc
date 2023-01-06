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
  std::unique_ptr<ClusterScheduler> scheduler;

protected:
public:
  ClusterManager(std::shared_ptr<GMemorySystem> gms, uint32_t cpuid, GProcessor *gproc);

  std::shared_ptr<Resource> getResource(Dinst *dinst) const { return scheduler->getResource(dinst); }
};
