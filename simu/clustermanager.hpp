// See LICENSE for details.

#pragma once

#include "clusterscheduler.hpp"
#include "depwindow.hpp"
#include "dinst.hpp"
#include "iassert.hpp"
#include "gmemorysystem.hpp"

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
