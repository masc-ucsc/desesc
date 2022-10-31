// See LICENSE for details.

#include "config.hpp"

#include "ClusterManager.h"
#include "ClusterScheduler.h"
#include "Cluster.h"
#include "GMemorySystem.h"
#include "Resource.h"

ClusterManager::ClusterManager(GMemorySystem *ms, uint32_t cpuid, GProcessor *gproc) {

  ResourcesPoolType res(iMAX);

  IN(forall((size_t i = 1; i < static_cast<size_t>(iMAX); i++), res[i].empty()));

  const char *coreSection = SescConf->getCharPtr("", "cpusimu", cpuid);
  if(coreSection == 0)
    return; // No core section, bad conf

  int32_t nClusters = SescConf->getRecordSize(coreSection, "cluster");

  for(int32_t i = 0; i < nClusters; i++) {
    const char *clusterName = SescConf->getCharPtr(coreSection, "cluster", i);
    SescConf->isCharPtr(coreSection, "cluster", i);

    Cluster *cluster = Cluster::create(clusterName, i, ms, cpuid, gproc);

    for(int32_t t = 0; t < iMAX; t++) {
      Resource *r = cluster->getResource(static_cast<InstOpcode>(t));

      if(r)
        res[t].push_back(r);
    }
  }

  const char *clusterScheduler = SescConf->getCharPtr(coreSection, "clusterScheduler");

  if(strcasecmp(clusterScheduler, "RoundRobin") == 0) {
    scheduler = new RoundRobinClusterScheduler(res);
  } else if(strcasecmp(clusterScheduler, "LRU") == 0) {
    scheduler = new LRUClusterScheduler(res);
  } else if(strcasecmp(clusterScheduler, "Use") == 0) {
    scheduler = new UseClusterScheduler(res);
  } else {
    MSG("ERROR: Invalid clusterScheduler [%s]", clusterScheduler);
    SescConf->notCorrect();
    return;
  }

  // 0 is an invalid opcde. All the other should be defined
  for(InstOpcode i = static_cast<InstOpcode>(1); i < iMAX; i = static_cast<InstOpcode>((int)i + 1)) {
    if(!res[i].empty())
      continue;

    MSG("ERROR: missing %s instruction type support", Instruction::opcode2Name(i));

    SescConf->notCorrect();
  }
}
