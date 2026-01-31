// See LICENSE for details.

#include "clusterscheduler.hpp"

#include "cluster.hpp"
#include "config.hpp"
#include "resource.hpp"

// #define DUMP_TRACE 1

ClusterScheduler::ClusterScheduler(const ResourcesPoolType& ores) : res(ores) {}

ClusterScheduler::~ClusterScheduler() {}

RoundRobinClusterScheduler::RoundRobinClusterScheduler(const ResourcesPoolType& ores) : ClusterScheduler(ores) {
  for (const auto op : Opcodes) {
    nres[op] = res[op].size();
  }
}

RoundRobinClusterScheduler::~RoundRobinClusterScheduler() {}

std::shared_ptr<Resource> RoundRobinClusterScheduler::getResource(Dinst* dinst) {
  const auto* inst = dinst->getInst();
  auto        op   = inst->getOpcode();

  unsigned int i = pos[op];
  if (i >= nres[op]) {
    i       = 0;
    pos[op] = 1;
  } else {
    pos[op]++;
  }

  I(i < res[op].size());

  return res[op][i];
}

LRUClusterScheduler::LRUClusterScheduler(const ResourcesPoolType& ores) : ClusterScheduler(ores) {}

LRUClusterScheduler::~LRUClusterScheduler() {}

std::shared_ptr<Resource> LRUClusterScheduler::getResource(Dinst* dinst) {
  const auto* inst = dinst->getInst();
  auto        op   = inst->getOpcode();

  std::shared_ptr<Resource> touse = res[op][0];

  for (size_t i = 1; i < res[op].size(); i++) {
    if (touse->getUsedTime() > res[op][i]->getUsedTime()) {
      touse = res[op][i];
    }
  }

  touse->setUsedTime();
  return touse;
}

UseClusterScheduler::UseClusterScheduler(const ResourcesPoolType& ores) : ClusterScheduler(ores) {
  for (const auto op : Opcodes) {
    nres[op] = res[op].size();
  }
}

UseClusterScheduler::~UseClusterScheduler() {}

std::shared_ptr<Resource> UseClusterScheduler::getResource(Dinst* dinst) {
  const auto* inst = dinst->getInst();
  auto        op   = inst->getOpcode();

  auto p = pos[op];
  if (p >= nres[op]) {
    p       = 0;
    pos[op] = 1;
  } else {
    pos[op]++;
  }

  auto touse = res[op][p];

  int touse_nintra = (cused[inst->getSrc1()] && cused[inst->getSrc1()]->get_id() != res[op][p]->getCluster()->get_id()) ? 1 : 0;
  touse_nintra += (cused[inst->getSrc2()] && cused[inst->getSrc2()]->get_id() != res[op][p]->getCluster()->get_id());

#if 0
  if (touse_nintra==0 && touse->getCluster()->getAvailSpace()>0 && touse->getCluster()->getNReady() <= 2)
    return touse;
#endif

  for (const auto i : Opcodes) {
    uint32_t n = p + static_cast<uint32_t>(i);
    if (n >= res[op].size()) {
      n -= res[op].size();
    }

    if (res[op][n]->getCluster()->getAvailSpace() <= 0) {
      continue;
    }

    int nintra = ((cused[inst->getSrc1()] != res[op][n]->getCluster() && cused[inst->getSrc1()]) ? 1 : 0)
                 + ((cused[inst->getSrc2()] != res[op][n]->getCluster() && cused[inst->getSrc2()]) ? 1 : 0);

#if 1
    if (nintra == touse_nintra && touse->getCluster()->getNReady() < res[op][n]->getCluster()->getNReady()) {
      touse        = res[op][n];
      touse_nintra = nintra;
    } else if (nintra < touse_nintra) {
      touse        = res[op][n];
      touse_nintra = nintra;
    }
#else
    if (touse->getCluster()->getAvailSpace() > res[op][n]->getCluster()->getAvailSpace()) {
      touse        = res[op][n];
      touse_nintra = nintra;
    }
#endif
  }

  cused[inst->getDst1()] = touse->getCluster();
  cused[inst->getDst2()] = touse->getCluster();
  I(cused[RegType::LREG_NoDependence] == 0);

  return touse;
}
