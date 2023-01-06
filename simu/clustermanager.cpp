// See LICENSE for details.

#include "clustermanager.hpp"

#include "cluster.hpp"
#include "clusterscheduler.hpp"
#include "gmemorysystem.hpp"
#include "resource.hpp"
#include "config.hpp"

ClusterManager::ClusterManager(std::shared_ptr<GMemorySystem> ms, uint32_t cpuid, GProcessor *gproc) {
  auto coreSection = Config::get_string("soc", "core", cpuid);
  auto nClusters   = Config::get_array_size(coreSection, "cluster");

  ResourcesPoolType res;
  for (auto i = 0u; i < nClusters; i++) {
    auto clusterName = Config::get_array_string(coreSection, "cluster", i);

    auto [cluster, new_res] = Cluster::create(clusterName, i, ms, cpuid, gproc);
    I(cluster);

    for (int32_t t = 0; t < iMAX; t++) {
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
  for (Opcode i = static_cast<Opcode>(1); i < iMAX; i = static_cast<Opcode>((int)i + 1)) {
    if (!res[i].empty()) {
      continue;
    }

    Config::add_error(fmt::format("core:{} does not support instruction type {}", coreSection, Instruction::opcode2Name(i)));
  }
}
