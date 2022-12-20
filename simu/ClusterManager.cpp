// See LICENSE for details.

#include "ClusterManager.h"

#include "Cluster.h"
#include "ClusterScheduler.h"
#include "GMemorySystem.h"
#include "Resource.h"
#include "config.hpp"

ClusterManager::ClusterManager(std::shared_ptr<GMemorySystem> ms, uint32_t cpuid, GProcessor *gproc) {
  auto coreSection = Config::get_string("soc", "core", cpuid);
  auto nClusters   = Config::get_array_size(coreSection, "cluster");

  ResourcesPoolType res(iMAX);
  for (auto i = 0u; i < nClusters; i++) {
    auto clusterName = Config::get_array_string(coreSection, "cluster", i);

    Cluster *cluster = Cluster::create(clusterName, i, ms, cpuid, gproc);

    for (int32_t t = 0; t < iMAX; t++) {
      Resource *r = cluster->getResource(static_cast<Opcode>(t));
      if (r) {
        res[t].push_back(r);
      }
    }
  }

  auto sched = Config::get_string(coreSection, "cluster_scheduler", {"RoundRobin", "LRU", "Use"});
  std::transform(sched.begin(), sched.end(), sched.begin(), [](unsigned char c) { return std::tolower(c); });

  if (sched == "roundrobin") {
    scheduler = new RoundRobinClusterScheduler(res);
  } else if (sched == "lru") {
    scheduler = new LRUClusterScheduler(res);
  } else if (sched == "use") {
    scheduler = new UseClusterScheduler(res);
  } else {
    Config::add_error(fmt::format("Invalid cluster_scheduler [{}]", sched));
    return;
  }

  // 0 is an invalid opcde. All the other should be defined
  for (Opcode i = static_cast<Opcode>(1); i < iMAX; i = static_cast<Opcode>((int)i + 1)) {
    if (!res[i].empty()) {
      continue;
    }

    Config::add_error(fmt::format("core:{} does not support instruction type {}", coreSection, Instruction::opcode2Name(i)));
  }
}
