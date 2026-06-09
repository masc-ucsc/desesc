// See LICENSE for details.

#include "depwindow.hpp"

#include "config.hpp"
#include "dinst.hpp"
#include "fmt/format.h"
#include "gprocessor.hpp"
#include "resource.hpp"
#include "tracer.hpp"

DepWindow::DepWindow(uint32_t cpuid, int src_id, const std::string& clusterName, uint32_t pos)
    : src_cluster_id(src_id), inter_cluster_fwd(fmt::format("P({})_{}{}_inter_cluster_fwd", cpuid, clusterName, pos)) {
  auto cadena    = fmt::format("P(P{}_{}{}_sched", cpuid, clusterName, pos);
  auto sched_num = Config::get_integer(clusterName, "sched_num");
  schedPort      = PortGeneric::create(cadena, sched_num, /*priority_managed=*/true);

  sched_lat         = Config::get_integer(clusterName, "sched_lat", 0, 32);
  inter_cluster_lat = Config::get_integer("soc", "core", cpuid, "inter_cluster_lat");
}

DepWindow::~DepWindow() {}

StallCause DepWindow::canIssue(Dinst* dinst) const {
  (void)dinst;
  return NoStall;
}

void DepWindow::add_inst(Dinst* dinst) {
  I(dinst->getCluster() != 0);  // Resource::schedule must set the resource field

  if (!dinst->hasDeps()) {
    dinst->set_in_cluster();
    preSelect(dinst);
  }
}

void DepWindow::preSelect(Dinst* dinst) {
  // At the end of the wakeUp, we can start to read the register file
  I(!dinst->hasDeps());

  // printf("DepWindow::::Preselect Entering preSelect Inst %llui at clock cycle %llu\n", dinst->getID(), globalClock);
  dinst->markIssued();
  // printf("DepWindow::::Preselect markIsssue Inst %llui at clock cycle %llu\n", dinst->getID(), globalClock);
  Tracer::stage(dinst, "WS");
  // printf("DepWindow::::Preselect WS done Inst %llui at clock cycle %llu\n", dinst->getID(), globalClock);

  I(dinst->getCluster());
  // printf("DepWindow::::Preselect Sending to Cluster Inst %llui at clock cycle %llu\n", dinst->getID(), globalClock);

  dinst->getCluster()->select(dinst);
}

void DepWindow::select(Dinst* dinst) {
  schedPort->schedule(dinst->has_stats(),
                      dinst->getID(),
                      dinst->isTransient(),
                      [this, dinst](Time_t allocated_time) { do_schedule(allocated_time, dinst); });
}

void DepWindow::do_schedule(Time_t when, Dinst* dinst) {
  // printf("DepWindow::::do_schedule Entering  Inst %llu at clock cycle %llu\n", dinst->getID(), globalClock);
  Time_t schedTime = when;
  if (dinst->hasInterCluster()) {
    // printf("DepWindow::::do_schedule   inter_cluster_lat Inst %llu at clock cycle %llu\n", dinst->getID(), globalClock);
    schedTime += inter_cluster_lat;
    // printf("DepWindow::::do_schedule::inter_cluster::  Inst %llu  schedTime= %llu at clock cycle %llu\n",
           // dinst->getID(),
           // schedTime,
           // globalClock);
  } else {
    // printf("DepWindow::::do_schedule sched_lat Inst %llu at clock cycle %llu\n", dinst->getID(), globalClock);
    schedTime += sched_lat;
    // printf("DepWindow::::do_schedule::scheduled::  Inst %llu  schedTime= %llu at clock cycle %llu\n",
           // dinst->getID(),
           // schedTime,
           // globalClock);
  }

  I(src_cluster_id == dinst->getCluster()->get_id());
  // only diff is the resource::receiving::schedTime same
  // printf("DepWindow:::do_shedule:: Sendingto  execution Inst %llui at clock cycle %llu\n", dinst->getID(), globalClock);
  Resource::executingCB::scheduleAbs(schedTime, dinst->getClusterResource().get(), dinst, dinst->getID());
}

void DepWindow::executed_flushed(Dinst* dinst) {

  if (dinst->isTransient()) {
    dinst->markExecutedTransient();
  } else {
    dinst->markExecuted();
  }

  dinst->clearRATEntry();
  // printf("Resource::DepWindow::Entering   dinst  %llu\n", dinst->getID());
  Tracer::stage(dinst, "WB");
}

// Called when dinst finished execution. Look for dependent to wakeUp
void DepWindow::executed(Dinst* dinst) {
  // printf("DepWindow::Executed:: Entering  executed for instID %llu at @Clockcycle %llu\n", dinst->getID(), globalClock);

  if (!dinst->isTransient()) {
    I(!dinst->hasDeps());
  }

  if (dinst->isTransient()) {
    dinst->markExecutedTransient();
  } else {
    dinst->markExecuted();
  }

  // printf("DepWindow::::Executed mark_executed Inst %llu\n", dinst->getID());
  dinst->clearRATEntry();
  // printf("DepWindow::::Executed clear RAT  Inst %llu\n", dinst->getID());
  // printf("Resource::DepWindow::Stage WB  dinst  %llu\n", dinst->getID());
  Tracer::stage(dinst, "WB");

  // if (!dinst->hasPending() || dinst->isTransient()) {
  if (!dinst->hasPending()) {  //(dinst->first !=0)
    return;
  }

  // NEVER HERE FOR in-order cores

  I(dinst->getCluster());
  I(src_cluster_id == dinst->getCluster()->get_id());

  I(dinst->isIssued());
  /*if(dinst->isTransient()){
    while (dinst->hasPending()){
      Dinst *dstReady = dinst->getNextPending();
      I(dstReady);
    }
    return;
  }*/

  while (dinst->hasPending()) {
    Dinst* dstReady = dinst->getNextPending();
    I(dstReady);
    if (dstReady->is_to_be_destroyed() && !dstReady->hasDeps()) {
      // dstReady->clear_to_be_destroyed_transient();
      dstReady->destroyTransientInst();
      continue;
    }

    I(!dstReady->isExecuted());

    if (!dstReady->hasDeps()) {
      // Check dstRes because dstReady may not be issued
      I(dstReady->getCluster());
      auto dst_cluster_id = dstReady->getCluster()->get_id();
      I(dst_cluster_id);
      // printf("DepWindow::::Executed  dstReady Inst is Inst %llu\n", dstReady->getID());
      // printf("DepWindow::::Executed DstReadyInst clusterID is  %d and src_cluster_id is %d\n", dst_cluster_id, src_cluster_id);

      if (dst_cluster_id != src_cluster_id) {
        // printf("DepWindow::::Executed DstReadyInst clusterID!=src_cluster_id::dst_cluster_id is %d and src_cluster_id is %d\n",
               // dst_cluster_id,
               // src_cluster_id);
        inter_cluster_fwd.inc(dstReady->has_stats());
        dstReady->markInterCluster();
        // printf("DepWindow::::Executed markInterCluster DstReadyInst markInterCluster for dstReady Inst %llu\n", dstReady->getID());
      } else {
        // printf("DepWindow::::Executed !markInterCluster DstReadyInst !markInterCluster for dstReady Inst %llu\n",
               // dstReady->getID());
      }

      // need todo resetInterCluster()
      // printf("DepWindow::::Executed dependency: DstReady sent to preselect :dstReadyInst %llu\n", dstReady->getID());
      preSelect(dstReady);
    }
  }
  // printf("DepWindow::Executed:: Leaving  executed for instID %llu at @Clockcycle %llu\n", dinst->getID(), globalClock);

  // dinst->flushfirst();//parent->first=0
}
