// See LICENSE for details.

#include "depwindow.hpp"

#include "config.hpp"
#include "dinst.hpp"
#include "fmt/format.h"
#include "gprocessor.hpp"
#include "resource.hpp"
#include "tracer.hpp"

DepWindow::DepWindow(uint32_t cpuid, int src_id, const std::string &clusterName, uint32_t pos)
    : src_cluster_id(src_id), inter_cluster_fwd(fmt::format("P({})_{}{}_inter_cluster_fwd", cpuid, clusterName, pos)) {
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
  Tracer::stage(dinst, "WS");

  I(dinst->getCluster());

  dinst->getCluster()->select(dinst);
}

void DepWindow::select(Dinst *dinst) {
  Time_t schedTime = schedPort->nextSlot(dinst->has_stats());
  if (dinst->hasInterCluster()) {
    schedTime += inter_cluster_lat;
  } else {
    schedTime += sched_lat;
  }

  I(src_cluster_id == dinst->getCluster()->get_id());

  Resource::executingCB::scheduleAbs(schedTime, dinst->getClusterResource().get(), dinst);  // NASTY to avoid callback ptr
}

void DepWindow::executed_flushed(Dinst *dinst) {
  //  MSG("execute [0x%x] @%lld",dinst, globalClock);

  if (dinst->isTransient()) {
    dinst->markExecutedTransient();
  } else {
    dinst->markExecuted();
  }

  dinst->clearRATEntry();
  Tracer::stage(dinst, "WB");
}

// Called when dinst finished execution. Look for dependent to wakeUp
void DepWindow::executed(Dinst *dinst) {
  //  MSG("execute [0x%x] @%lld",dinst, globalClock);

  if (!dinst->isTransient()) {
    I(!dinst->hasDeps());
  }

  if (dinst->isTransient()) {
    dinst->markExecutedTransient();
  } else {
    dinst->markExecuted();
  }

  dinst->clearRATEntry();
  Tracer::stage(dinst, "WB");

  // if (!dinst->hasPending() || dinst->isTransient()) {
  if (!dinst->hasPending()) {
    return;
  }

  // NEVER HERE FOR in-order cores

  I(dinst->getCluster());
  I(src_cluster_id == dinst->getCluster()->get_id());

  I(dinst->isIssued());

  while (dinst->hasPending()) {
    Dinst *dstReady = dinst->getNextPending();
    I(dstReady);

    I(!dstReady->isExecuted());

    if (!dstReady->hasDeps()) {
      // Check dstRes because dstReady may not be issued
      I(dstReady->getCluster());
      auto dst_cluster_id = dstReady->getCluster()->get_id();
      I(dst_cluster_id);

      if (dst_cluster_id != src_cluster_id) {
        inter_cluster_fwd.inc(dstReady->has_stats());
        dstReady->markInterCluster();
      }

      preSelect(dstReady);
    }
  }
}
