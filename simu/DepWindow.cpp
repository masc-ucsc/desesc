// See LICENSE for details.

#include "DepWindow.h"

#include "GProcessor.h"
#include "Resource.h"
#include "config.hpp"
#include "dinst.hpp"
#include "fmt/format.h"

DepWindow::DepWindow(uint32_t cpuid, Cluster *aCluster, const std::string &clusterName, uint32_t pos)
    : srcCluster(aCluster), inter_cluster_fwd(fmt::format("P({})_{}{}_inter_cluster_fwd", cpuid, clusterName, pos)) {
  auto cadena    = fmt::format("P(P{}_{}{}_sched", cpuid, clusterName, pos);
  auto sched_num = Config::get_integer(clusterName, "sched_num");
  auto sched_occ = Config::get_integer(clusterName, "sched_occ");
  schedPort      = PortGeneric::create(cadena, sched_num, sched_occ);

  sched_lat         = Config::get_integer(clusterName, "sched_lat", 0, 32);
  inter_cluster_lat = Config::get_integer("soc", "core", cpuid, "inter_cluster_lat");
}

DepWindow::~DepWindow() {}

StallCause DepWindow::canIssue(Dinst *dinst) const {
  (void)dinst;
  return NoStall;
}

void DepWindow::add_inst(Dinst *dinst) {
  I(dinst->getCluster() != 0);  // Resource::schedule must set the resource field

  if (!dinst->hasDeps()) {
    preSelect(dinst);
  }
}

void DepWindow::preSelect(Dinst *dinst) {
  // At the end of the wakeUp, we can start to read the register file
  I(!dinst->hasDeps());

  dinst->markIssued();
  I(dinst->getCluster());

  dinst->getCluster()->select(dinst);
}

void DepWindow::select(Dinst *dinst) {
  Time_t schedTime = schedPort->nextSlot(dinst->getStatsFlag());
  if (dinst->hasInterCluster())
    schedTime += inter_cluster_lat;
  else
    schedTime += sched_lat;

  I(srcCluster == dinst->getCluster());

  Resource::executingCB::scheduleAbs(schedTime, dinst->getClusterResource(), dinst);
}

// Called when dinst finished execution. Look for dependent to wakeUp
void DepWindow::executed(Dinst *dinst) {
  //  MSG("execute [0x%x] @%lld",dinst, globalClock);

  I(!dinst->hasDeps());

  dinst->markExecuted();
  dinst->clearRATEntry();

  if (!dinst->hasPending())
    return;

  // NEVER HERE FOR in-order cores

  I(dinst->getCluster());
  I(srcCluster == dinst->getCluster());

  I(dinst->isIssued());
  while (dinst->hasPending()) {
    Dinst *dstReady = dinst->getNextPending();
    I(dstReady);

    I(!dstReady->isExecuted());

    if (!dstReady->hasDeps()) {
      // Check dstRes because dstReady may not be issued
      I(dstReady->getCluster());
      const Cluster *dstCluster = dstReady->getCluster();
      I(dstCluster);

      if (dstCluster != srcCluster) {
        inter_cluster_fwd.inc(dstReady->getStatsFlag());
        dstReady->markInterCluster();
      }

      preSelect(dstReady);
    }
  }
}
