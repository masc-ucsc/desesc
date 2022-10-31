// See LICENSE for details.

#include "dinst.hpp"
#include "config.hpp"

#include "DepWindow.h"
#include "GProcessor.h"
#include "Resource.h"

DepWindow::DepWindow(uint32_t cpuid, Cluster *aCluster, const char *clusterName, uint32_t pos)
    : srcCluster(aCluster)
    , Id(cpuid)
    , wrForwardBus("P(%d)_%s%d_wrForwardBus", cpuid, clusterName, pos) {
  char cadena[1024];

  sprintf(cadena, "P(%d)_%s%d_sched", cpuid, clusterName, pos);
  schedPort =
      PortGeneric::create(cadena, SescConf->getInt(clusterName, "SchedNumPorts"), SescConf->getInt(clusterName, "SchedPortOccp"));

  InterClusterLat = SescConf->getInt("cpusimu", "interClusterLat", cpuid);
  SchedDelay      = SescConf->getInt(clusterName, "schedDelay");

  // Constraints
  SescConf->isInt(clusterName, "schedDelay");
  SescConf->isBetween(clusterName, "schedDelay", 0, 1024);

  SescConf->isInt("cpusimu", "interClusterLat", cpuid);
  SescConf->isBetween("cpusimu", "interClusterLat", SchedDelay, 1024, cpuid);
}

DepWindow::~DepWindow() {
}

StallCause DepWindow::canIssue(Dinst *dinst) const {

  return NoStall;
}

void DepWindow::addInst(Dinst *dinst) {

  I(dinst->getCluster() != 0); // Resource::schedule must set the resource field

  if(!dinst->hasDeps()) {
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
  if(dinst->hasInterCluster())
    schedTime += InterClusterLat;
  else
    schedTime += SchedDelay;

  I(srcCluster == dinst->getCluster());

  Resource::executingCB::scheduleAbs(schedTime, dinst->getClusterResource(), dinst);
}

// Called when dinst finished execution. Look for dependent to wakeUp
void DepWindow::executed(Dinst *dinst) {
  //  MSG("execute [0x%x] @%lld",dinst, globalClock);

  I(!dinst->hasDeps());

  dinst->markExecuted();
  dinst->clearRATEntry();

  if(!dinst->hasPending())
    return;

  // NEVER HERE FOR in-order cores

  I(dinst->getCluster());
  I(srcCluster == dinst->getCluster());

  I(dinst->isIssued());
  while(dinst->hasPending()) {
    Dinst *dstReady = dinst->getNextPending();
    I(dstReady);

    I(!dstReady->isExecuted());

    if(!dstReady->hasDeps()) {
      // Check dstRes because dstReady may not be issued
      I(dstReady->getCluster());
      const Cluster *dstCluster = dstReady->getCluster();
      I(dstCluster);

      if(dstCluster != srcCluster) {
        wrForwardBus.inc(dstReady->getStatsFlag());
        dstReady->markInterCluster();
      }

      preSelect(dstReady);
    }
  }
}
