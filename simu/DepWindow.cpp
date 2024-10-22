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

//newHEAD<<<<<<< HEAD
//<<<<<<< HEAD
  printf("DepWindow::add_inst before dinst->hasDeps() %ld  and transient is %b\n",
      dinst->getID(),dinst->isTransient());
  //while (!dinst->hasDeps()) { //makesure hasdeps==0)lima 
 if (!dinst->hasDeps()) {
    dinst->set_in_cluster();
    preSelect(dinst);
  } 
  else {
    printf("2024:::DepWindow::add_inst dinst->hasDeps() is true for  %ld  and transient is %b\n",
        dinst->getID(), dinst->isTransient());
    //while (!dinst->hasDeps()){
      //preSelect(dinst);
    //}
/*=======
  //printf("DepWindow::add_inst before dinst->hasDeps() %ld  and transient is %b\n", dinst->getID(),dinst->isTransient());
  
  if (!dinst->hasDeps()) {
    preSelect(dinst);
  } else {
    //printf("DepWindow::add_inst dinst->hasDeps() is true for  %ld  and transient is %b\n", dinst->getID(), dinst->isTransient());
>>>>>>> upstream/main*/
/*=======
  if (!dinst->hasDeps()) {
    preSelect(dinst);
>>>>>>> upstream/main*/
  }
 
}

void DepWindow::preSelect(Dinst *dinst) {
  // At the end of the wakeUp, we can start to read the register file
  I(!dinst->hasDeps());

  printf("DepWindow::::Preselect WS Inst %ld\n", dinst->getID());
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
//<<<<<<< HEAD
 
  if(dinst->isTransient())
    printf("DepWindow::::Executed Entering Transient Inst %ld\n", dinst->getID());
  //if(!dinst->isTransient()){
//=======

  if (!dinst->isTransient()) {
//>>>>>>> upstream/main
    I(!dinst->hasDeps());
  }

  if (dinst->isTransient()) {
    dinst->markExecutedTransient();
  } else {
    dinst->markExecuted();
  }

  printf("DepWindow::::Executed mark_executed Inst %ld\n", dinst->getID());
  dinst->clearRATEntry();
  printf("DepWindow::::Executed clear RAT  Inst %ld\n", dinst->getID());
  Tracer::stage(dinst, "WB");
  printf("DepWindow::::Executed stage WB Inst %ld\n", dinst->getID());

 //if (!dinst->hasPending() || dinst->isTransient()) {
  if (!dinst->hasPending()) { //(dinst->first !=0)
    printf("DepWindow::::Executed Inst %ld has !dinst->hasPending\n", dinst->getID());
    return;
  }


/*//<<<<<<< HEAD
 //if (!dinst->hasPending() || dinst->isTransient()) {
  if (!dinst->hasPending()) { //(dinst->first !=0)
    printf("DepWindow::::Executed Inst %ld has !dinst->hasPending\n", dinst->getID());
//=======
  dinst->clearRATEntry();
  Tracer::stage(dinst, "WB");

  // if (!dinst->hasPending() || dinst->isTransient()) {
  if (!dinst->hasPending()) {
>>>>>>> upstream/main
    return;
  }*/

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
    Dinst *dstReady = dinst->getNextPending();
    I(dstReady);
    std::cout<<"Depwindow_Jose:: Executed::hasPending():: executed_dinst dstReadyInst asm is "<<dstReady->getInst()->get_asm()<<std::endl;
    std::cout<<"Depwindow_Jose:: executed::dstReady Inst asm is "<<dstReady->getInst()->get_asm()<<std::endl;
    printf("DepWindow_Jose::::Executed Inst is %ld and Pending dstdReady inst is %ld and Pending :isTransient is %b\n",
        dinst->getID(),dstReady->getID(),dstReady->isTransient());
     
    
    if(dstReady->is_to_be_destroyed() && !dstReady->hasDeps()) {
      //dstReady->clear_to_be_destroyed_transient();
      dstReady->destroyTransientInst();
      continue;
      } 
       
    

    I(!dstReady->isExecuted());
    

//<<<<<<< HEAD
    printf("DepWindow::::Executed Inst is %ld and Pending Inst is %ld and Pending :isTransient is %b\n",
        dinst->getID(),dstReady->getID(),dstReady->isTransient());
    std::cout<<"Depwindow:: Executed::hasPending():: iexecuted_dinst Inst asm is "<<dinst->getInst()->get_asm()<<std::endl;
    std::cout<<"Depwindow:: executed::dstReady Inst asm is "<<dstReady->getInst()->get_asm()<<std::endl;
    printf("Depwindow:: executed::dstReady has ndeps is: %d\n",(int)dstReady->getnDeps());

//=======
    //printf("DepWindow::::Executed Inst is %ld and Pending Inst is %ld and Pending :isTransient is %b\n", dinst->getID(),dstReady->getID(),dstReady->isTransient());
//>>>>>>> upstream/main
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

  //dinst->flushfirst();//parent->first=0
 printf("DepWindow::::Executed Exiting Transient Inst %ld\n", dinst->getID());
}
