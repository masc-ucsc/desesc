// See LICENSE for details.

#include "clustermanager.hpp"

#include "cluster.hpp"
#include "clusterscheduler.hpp"
#include "config.hpp"
#include "gmemory_system.hpp"
#include "resource.hpp"

ClusterManager::ClusterManager(std::shared_ptr<Gmemory_system> ms, uint32_t cpuid, GProcessor *gproc) {
  auto coreSection = Config::get_string("soc", "core", cpuid);
  auto nClusters   = Config::get_array_size(coreSection, "cluster");

  ResourcesPoolType res;
  for (auto i = 0u; i < nClusters; i++) {
    auto clusterName = Config::get_array_string(coreSection, "cluster", i);

    auto [cluster, new_res] = Cluster::create(clusterName, i, ms, cpuid, gproc);
    I(cluster);

    for (const auto t : Opcodes) {
      if (new_res[t]) {
        res[t].push_back(new_res[t]);
      }
    }
  }

  auto sched = Config::get_string(coreSection, "cluster_scheduler", {"RoundRobin", "LRU", "Use"});
  std::transform(sched.begin(), sched.end(), sched.begin(), [](unsigned char c) { return std::tolower(c); });

  if (sched == "roundrobin") {
    scheduler = std::make_unique<RoundRobinClusterScheduler>(res);
  } else if (sched == "lru") {
    scheduler = std::make_unique<LRUClusterScheduler>(res);
  } else if (sched == "use") {
    scheduler = std::make_unique<UseClusterScheduler>(res);
  } else {
    Config::add_error(fmt::format("Invalid cluster_scheduler [{}]", sched));
    return;
  }

  // 0 is an invalid opcde. All the other should be defined
  for (const auto i : Opcodes) {
    if (!res[i].empty()) {
      continue;
    }

    Config::add_error(fmt::format("core:{} does not support instruction type {}", coreSection, i));
  }
}
