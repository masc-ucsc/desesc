// See LICENSE for details.

#include "ClusterScheduler.h"

#include "Cluster.h"
#include "Resource.h"
#include "config.hpp"

// #define DUMP_TRACE 1

ClusterScheduler::ClusterScheduler(const ResourcesPoolType &ores) : res(ores) {}

ClusterScheduler::~ClusterScheduler() {}

RoundRobinClusterScheduler::RoundRobinClusterScheduler(const ResourcesPoolType &ores) : ClusterScheduler(ores) {
  nres.resize(res.size());
  pos.resize(res.size(), 0);

  for (size_t i = 0; i < res.size(); i++) {
    nres[i] = res[i].size();
  }
}

RoundRobinClusterScheduler::~RoundRobinClusterScheduler() {}

Resource *RoundRobinClusterScheduler::getResource(Dinst *dinst) {
  const Instruction *inst = dinst->getInst();
  Opcode             type = inst->getOpcode();

  unsigned int i = pos[type];
  if (i >= nres[type]) {
    i         = 0;
    pos[type] = 1;
  } else {
    pos[type]++;
  }

  I(i < res[type].size());

  return res[type][i];
}

LRUClusterScheduler::LRUClusterScheduler(const ResourcesPoolType &ores) : ClusterScheduler(ores) {}

LRUClusterScheduler::~LRUClusterScheduler() {}

Resource *LRUClusterScheduler::getResource(Dinst *dinst) {
  const Instruction *inst  = dinst->getInst();
  Opcode             type  = inst->getOpcode();
  Resource          *touse = res[type][0];

  for (size_t i = 1; i < res[type].size(); i++) {
    if (touse->getUsedTime() > res[type][i]->getUsedTime()) {
      touse = res[type][i];
    }
  }

  touse->setUsedTime();
  return touse;
}

UseClusterScheduler::UseClusterScheduler(const ResourcesPoolType &ores) : ClusterScheduler(ores) {
  nres.resize(res.size());
  pos.resize(res.size(), 0);

  for (size_t i = 0; i < res.size(); i++) {
    nres[i] = res[i].size();
  }
  for (size_t i = 0; i < LREG_MAX; i++) {
    cused[i] = 0;
  }
}

UseClusterScheduler::~UseClusterScheduler() {}

Resource *UseClusterScheduler::getResource(Dinst *dinst) {
  const Instruction *inst = dinst->getInst();
  Opcode             type = inst->getOpcode();

  unsigned int p = pos[type];
  if (p >= nres[type]) {
    p         = 0;
    pos[type] = 1;
  } else {
    pos[type]++;
  }

  Resource *touse = res[type][p];

  int touse_nintra = (cused[inst->getSrc1()] != res[type][p]->getCluster() && cused[inst->getSrc1()])
                     + (cused[inst->getSrc2()] != res[type][p]->getCluster() && cused[inst->getSrc2()]);

#if 0
  if (touse_nintra==0 && touse->getCluster()->getAvailSpace()>0 && touse->getCluster()->getNReady() <= 2)
    return touse;
#endif

  for (size_t i = 0; i < res[type].size(); i++) {
    uint16_t n = p + i;
    if (n >= res[type].size()) {
      n -= res[type].size();
    }

    if (res[type][n]->getCluster()->getAvailSpace() <= 0) {
      continue;
    }

    int nintra = (cused[inst->getSrc1()] != res[type][n]->getCluster() && cused[inst->getSrc1()])
                 + (cused[inst->getSrc2()] != res[type][n]->getCluster() && cused[inst->getSrc2()]);

#if 1
    if (nintra == touse_nintra && touse->getCluster()->getNReady() < res[type][n]->getCluster()->getNReady()) {
      // if (nintra == touse_nintra && touse->getCluster()->getAvailSpace() < res[type][n]->getCluster()->getAvailSpace()) {
      // Same # deps, pick the less busy
      touse        = res[type][n];
      touse_nintra = nintra;
    } else if (nintra < touse_nintra) {
      touse        = res[type][n];
      touse_nintra = nintra;
    }
#else
    if (touse->getCluster()->getAvailSpace() > res[type][n]->getCluster()->getAvailSpace()) {
      touse        = res[type][n];
      touse_nintra = nintra;
    }
#endif
  }

  cused[inst->getDst1()] = touse->getCluster();
  cused[inst->getDst2()] = touse->getCluster();
  I(cused[LREG_NoDependence] == 0);

  return touse;
}
