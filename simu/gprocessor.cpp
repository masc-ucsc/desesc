//BISMILLAH HIR RAHMAR NIR RAHIM
// See LICENSE for details.
#include "gprocessor.hpp"

#include <sys/time.h>
#include <unistd.h>
#include <string> 
#include "fetchengine.hpp"
#include "fmt/format.h"
#include "gmemory_system.hpp"
#include "report.hpp"
#include "tracer.hpp"

GProcessor::GProcessor(std::shared_ptr<Gmemory_system> gm, Hartid_t i)
    : Simu_base(gm, i)
    , FetchWidth(Config::get_integer("soc", "core", i, "fetch_width"))
    , IssueWidth(Config::get_integer("soc", "core", i, "issue_width"))
    , RetireWidth(Config::get_integer("soc", "core", i, "retire_width"))
    , RealisticWidth(RetireWidth < IssueWidth ? RetireWidth : IssueWidth)
    , InstQueueSize(Config::get_integer("soc", "core", i, "instq_size"))
    , MaxROBSize(Config::get_integer("soc", "core", i, "rob_size", 4))
    , memorySystem(gm)
    , rROB(Config::get_integer("soc", "core", i, "rob_size"))
    , ROB(MaxROBSize)
    , avgFetchWidth(fmt::format("P({})_avgFetchWidth", i))
    , rrobUsed(fmt::format("({})_rrobUsed", i))  // avg
    , robUsed(fmt::format("({})_robUsed", i))    // avg
    , nReplayInst(fmt::format("({})_nReplayInst", i))
    , nCommitted(fmt::format("({}):nCommitted", i))  // Should be the same as robUsed - replayed
    , noFetch(fmt::format("({}):noFetch", i))
    , noFetch2(fmt::format("({}):noFetch2", i))
    , pipeQ(i) {
  smt_size = Config::get_integer("soc", "core", i, "smt", 1, 32);

  lastReplay = 0;

  nStall[SmallWinStall]     = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nSmallWinStall", i));
  nStall[SmallROBStall]     = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nSmallROBStall", i));
  nStall[SmallREGStall]     = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nSmallREGStall", i));
  nStall[DivergeStall]      = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nDivergeStall", i));
  nStall[OutsLoadsStall]    = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nOutsLoadsStall", i));
  nStall[OutsStoresStall]   = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nOutsStoresStall", i));
  nStall[OutsBranchesStall] = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nOutsBranchesStall", i));
  nStall[ReplaysStall]      = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nReplaysStall", i));
  nStall[SyscallStall]      = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nSyscallStall", i));

  I(ROB.size() == 0);

  buildInstStats("ExeEngine");

#ifdef WAVESNAP_EN
  snap = std::make_unique<Wavesnap>();
#endif

  scb        = std::make_shared<Store_buffer>(i, gm);
  storeset   = std::make_shared<StoreSet>(i);
  prefetcher = std::make_shared<Prefetcher>(gm->getDL1(), i);

  use_stats = false;

  smt_fetch.fe.emplace_back(std::make_unique<FetchEngine>(i, gm));

  for (auto n = 1u; n < smt_size; ++n) {
    smt_fetch.fe.emplace_back(std::make_unique<FetchEngine>(i, gm, smt_fetch.fe[0]->ref_bpred()));
  }

  spaceInInstQueue = InstQueueSize;

  busy = false;
}

GProcessor::~GProcessor() {}

void GProcessor::buildInstStats(const std::string &txt) {
  for (const auto t : Opcodes) {
    nInst[t] = std::make_unique<Stats_cntr>(fmt::format("P({})_{}_{}:n", hid, txt, t));
  }
}

void SMT_fetch::update() {
  if (smt_lastTime != globalClock) {
    smt_lastTime = globalClock;
    smt_active   = smt_cnt;
    smt_cnt      = 1;
  } else {
    smt_turn++;
  }
  I(smt_active > 0);

  smt_turn--;
  if (smt_turn < 0) {
    if (smt_cnt == smt_active) {
      smt_turn = 0;
    } else {
      smt_turn = smt_active - 1;
    }
  }
}

std::shared_ptr<FetchEngine> SMT_fetch::fetch_next() {
  auto ptr = fe[smt_turn];

  update();

  return ptr;
}

void GProcessor::fetch() {
  // TODO: Move this to GProcessor (same as in OoOProcessor)
  printf("gprocessor::fetch \n");
  I(eint);
  I(is_power_up());

  if (spaceInInstQueue < FetchWidth) {
    return;
  }

  auto ifid = smt_fetch.fetch_next();
 //must be before *bucket 
  /*
  if (ifid->isBlocked()) {
    // fmt::print("fetch on {}\n", ifid->getMissDinst()->getID());
     return;
  }
*/

  auto     smt_hid = hid;  // FIXME: do SMT fetch
  IBucket *bucket  = pipeQ.pipeLine.newItem();

    if(ifid->isBlocked()) {
        Addr_t pc = ifid->getMissDinst()->getAddr() + 4;
        printf("gprocessor::fetch on branchmiss{}%ld\n",ifid->get_miss_dinst()->getAddr());
        printf("gprocessor::fetch on branchmiss + 4 {}%ld\n",pc);
       //printf("gprocessor::fetch on branchmiss{}%ld\n", ifid->getMissDinst()->getAddr());
       
       /*auto *dinst_br= ifid->getMissDinst();
       if(dinst_br->getCluster()->get_reg_pool() >= dinst_br->getCluster()->get_nregs()-7) {
         return;
       }*/
       if (bucket) 
         add_inst_transient_on_branch_miss(bucket, pc);
         
       return; 
  }
    //enable only when !ifid->isblocked() to flush transient before every new fetch 
    //after a branch miss-prediction resolved at execute stage
    if(ifid->is_fetch_next_ready) {
      flush_transient_inst_on_fetch_ready();
    }
      
  if (bucket) {
    ifid->fetch(bucket, eint, smt_hid);
    if (!bucket->empty()) {
      printf("gprocessor::fetch:: completed bucket size is %ld\n",bucket->size());
      avgFetchWidth.sample(bucket->size(), bucket->top()->has_stats());
      busy = true;
    }
  }
}
void GProcessor::flush_transient_inst_on_fetch_ready() {
 
  pipeQ.pipeLine.flush_transient_inst_from_buffer();
  flush_transient_inst_from_inst_queue();
  flush_transient_from_rob();
}


void GProcessor::flush_transient_from_rob() {
//try the for loop scan

  while(!ROB.empty()) {
    auto *dinst = ROB.end_data();
    //makes sure isExecuted in preretire()
    
    if (!dinst->isTransient()) {
      ROB.push_pipe_in_cluster(dinst);
      ROB.pop_from_back();
      printf("GPROCCESOR::flush_Rob :: !dinst->isTransient() ROB=>pipe_in_cluster instID %ld\n", dinst->getID());  
      continue;
    }
    if(dinst->getCluster()->get_reg_pool() >= dinst->getCluster()->get_nregs()-2) {
      break;
        }

   
   if( dinst->getCluster()->get_window_size() == dinst->getCluster()->get_window_maxsize()){
     break;
   }
    /*if (dinst->hasDeps() || dinst->hasPending()) { 
      ROB.push_pipe_in_cluster(dinst);
      ROB.pop_from_back();
      printf("GPROCCESOR::flush_Rob :: dinst->hasDeps() || dinst->hasPending() instID %ld\n", dinst->getID());  
      continue;
    }*/

    printf("GPROCCESOR::flush_Rob :: before checking status  instID %ld\n", dinst->getID());  
    if(!dinst->isRetired() && dinst->isExecuted()) {
       // dinst->clearRATEntry();
        while (dinst->hasPending()) {
          printf("GPROCCESOR::flush_Rob :: isRetired() Pending for instID %ld\n", dinst->getID());  
          Dinst *dstReady = dinst->getNextPending();
          I(dstReady->isTransient());
        }
        bool hasDest = (dinst->getInst()->hasDstRegister());
        if (hasDest) {
          printf("GPROCCESOR::flush_Rob :: isRetired()  regpool++ destroying for instID %ld\n", dinst->getID());  
          dinst->getCluster()->add_reg_pool();
        }
        dinst->clearRATEntry();
        dinst->destroyTransientInst();
    } else if (dinst->isExecuting() || dinst->isIssued()) {
        dinst->mark_flush_transient();
        printf("GPROCCESOR::flush_Rob ::mark flush isExecuting || isIssued instID %ld\n", dinst->getID()); 
        while (dinst->hasPending()) {
          printf("GPROCCESOR::flush_Rob mark flush  Pending for instID %ld\n", dinst->getID());  
          Dinst *dstReady = dinst->getNextPending();
          I(dstReady->isTransient());
        }

        ROB.push_pipe_in_cluster(dinst);
    } else if (dinst->isRenamed()) {
        printf("GPROCCESOR::flush_Rob :: isTransient and (dinst->isRenamed()  instID %ld\n", dinst->getID());  
        //Rename :RN  
        if( dinst->getCluster()->get_reg_pool() >= dinst->getCluster()->get_nregs()-3){
          printf("GPROCCESOR::flush_Rob :: isTransient and (dinst->isRenamed() reg_pool>nregs instID %ld\n", dinst->getID());  
          while (dinst->hasPending()) {
            Dinst *dstReady = dinst->getNextPending();
            I(dstReady->isTransient());
        }
          break;
        }
        printf("GPROCCESOR::flush_Rob : isRenamed instID %ld\n", dinst->getID());  
        dinst->markExecutedTransient();
        //dinst->clearRATEntry();
        printf("GPROCCESOR::flush_Rob : clear RAT entry instID %ld\n", dinst->getID());  
        while (dinst->hasPending()) {
          Dinst *dstReady = dinst->getNextPending();
          I(dstReady->isTransient());
        }
        //printf("GPROCCESOR::flush_Rob :: has pending  instID %ld\n", dinst->getID());  
        
        bool hasDest = (dinst->getInst()->hasDstRegister());
        if (hasDest) {
          printf("GPROCCESOR::flush_Rob : isRename regPool++ instID %ld\n", dinst->getID());  
          dinst->getCluster()->add_reg_pool();
        }

        dinst->clearRATEntry();
        Dinst* dinst1 = dinst->getParentSrc1();
        if(dinst1) {
          dinst1->flush_first();
        }
        Dinst* dinst2 = dinst->getParentSrc2();
        if(dinst2) {
          dinst2->flush_first();
        }
        dinst->getCluster()->delEntry();
        dinst->destroyTransientInst();
    }
            
    ROB.pop_from_back();
    }
  while(!ROB.empty_pipe_in_cluster()) {
    auto *dinst = ROB.back_pipe_in_cluster();//get last element from vector:back()
    printf("GPROCCESOR::flush_Rob : Reading  ROB_pipe_in_cluster instID %ld\n", dinst->getID());  

    /*if(dinst->getCluster()->get_reg_pool() >= dinst->getCluster()->get_nregs()-7) {
          ROB.push(dinst);//push in the end of ROB
          ROB.pop_pipe_in_cluster();//pop last element from buffer_ROB
          printf("GPROCCESOR::flush_Rob : ROB_pipe_in_cluster regpool >nregs instID %ld\n", dinst->getID());  
          continue;
        }*/

    /*if(dinst->getCluster()->get_reg_pool() >= dinst->getCluster()->get_nregs()-2) {
          ROB.push(dinst);//push in the end of ROB
          ROB.pop_pipe_in_cluster();//pop last element from buffer_ROB
          printf("GPROCCESOR::flush_Rob : ROB_pipe_in_cluster regpool >nregs instID %ld\n", dinst->getID());  
          continue;
    }*/


    if(dinst->is_flush_transient() && dinst->isExecuted() && !dinst->hasDeps() && !dinst->hasPending()) { 
      if( dinst->getCluster()->get_window_size() < dinst->getCluster()->get_window_maxsize()-1) {
        
        bool hasDest = (dinst->getInst()->hasDstRegister());
        if (hasDest) {
          dinst->getCluster()->add_reg_pool();
          printf("GPROCCESOR::flush_Rob ::ROB->BufferROB regPool++ instID %ld\n", dinst->getID());  
        }
        dinst->markExecutedTransient();
        dinst->clearRATEntry();
        printf("GPROCCESOR::flush_Rob ::destroying Inst from BufferROB. Not putting in ROB instID %ld\n", 
          dinst->getID());  
        dinst->getCluster()->delEntry();
        dinst->destroyTransientInst();
      }
    } else {
        printf("GPROCCESOR::flush_Rob :: ROB.pipeincluster=> pushing back in ROB instID %ld\n", 
          dinst->getID());  
        ROB.push(dinst);//push in the end of ROB
    }
    ROB.pop_pipe_in_cluster();//pop last element from buffer_ROB
  }
}


/*void GProcessor::flush_transient_from_rob() {
//try the for loop scan
  while(!ROB.empty()) {
    //auto *dinst = ROB.top();
    printf("GPROCCESOR::ROB FLUSHING  ENTERING \n");  
    auto *dinst = ROB.end_data();
    //if (!dinst->isTransient())
      //break;
    printf("GPROCCESOR::ROB FLUSHING :: instID %ld\n", dinst->getID());  
    //makes sure isExecuted in preretire()
    bool  done  = dinst->getClusterResource()->preretire(dinst, false);
    if (!done) {
      //break;//FIXME
      //ROB.pop();
      ROB.push_pipe_in_cluster(dinst);
      ROB.pop_from_back();
      continue;
    }

    bool done_cluster = dinst->getCluster()->retire(dinst, false);

    if (!done_cluster) {
      //break;
      //ROB.pop(); 
      ROB.push_pipe_in_cluster(dinst);
      ROB.pop_from_back();
      continue;
    }
    if (dinst->isTransient()) {
         printf("GPROCCESOR::ROB FLUSHING destroying_ROB transient instID %ld\n", dinst->getID());  
         dinst->destroyTransientInst();
    } 

  ROB.pop_from_back();    
  }

  while(!ROB.empty_pipe_in_cluster()) {
    auto *dinst = ROB.back_pipe_in_cluster();//get last element from vector:back()
    ROB.pop_pipe_in_cluster();//pop last element
    ROB.push(dinst);//push in the end
  }


}
*/

void GProcessor::flush_transient_inst_from_inst_queue() {
    
    //printf("PipeQ::Entering ::instqueue size is %lu\n",pipeQ.instQueue.size()); 
    
    while(!pipeQ.instQueue.empty()) {
    
    //printf("PipeQ::flush::!buffer.empty () buffer size inside is %lu\n",pipeQ.instQueue.size()); 
    auto *bucket = pipeQ.instQueue.top();
    if (bucket) {
      while(!bucket->empty()) {
        auto *dinst = bucket->top();
        printf("Gprocess::flush_inst_queue::instqueue.size is %lu and instID %ld\n",bucket->size(), 
            dinst->getID()); 
        bucket->pop();
        //I(dinst->isTransient());
        if (dinst->isTransient() && !dinst->is_present_in_rob()) {
         printf("Gprocess::flush_inst_queue::instqueue.size is %lu and  destroying transient instID %ld\n",bucket->size(), 
         dinst->getID());  
        // noneed:dinst->clearRATEntry();
         dinst->destroyTransientInst();
        }
      }
      if(bucket->empty()) {//FIXME
       printf("Gprocess::flush_inst_queue::bucket.empty:: so added back to bucketPool \n"); 
        I(bucket->empty());
        pipeQ.pipeLine.doneItem(bucket);
      }
    }
    pipeQ.instQueue.pop();
    }
  }


Addr_t GProcessor::random_addr_Gen(){
      Addr_t addr = 0x200;
      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_int_distribution<> dis(1, 100);
      int randomNumber = dis(gen);
      return addr+(uint64_t)randomNumber;
}


void GProcessor:: add_inst_transient_on_branch_miss(IBucket *bucket, Addr_t pc) {
       int i=0;
       //string a="LREG_R31";
       while (i< FetchWidth/2) {
       //while (i< 3) {
         //printf("gProcessor:: Entering transient Inst\n");
         //auto  *alu_dinst = Dinst::create(Instruction(Opcode::iAALU, RegType::LREG_R3, RegType::LREG_R3, RegType::LREG_R3, RegType::LREG_R3)
         /*auto  *alu_dinst = Dinst::create(Instruction(Opcode::iAALU, RegType::LREG_R31, RegType::LREG_R30, RegType::LREG_R29, RegType::LREG_R31)
                                    ,pc
                                    ,0
                                    ,0
                                    ,true);*/
       
                                    
         
       //Addr_t addr    = random_addr_Gen();
       /*auto *alu_dinst= Dinst::create(Instruction(Opcode::iSALU_ST, RegType::LREG_R5, RegType::LREG_R6, RegType::LREG_R7, RegType::LREG_R8)
                      ,pc
                      ,addr
                      ,0
                      ,true);*/
    
       /*auto *alu_dinst= Dinst::create(Instruction(Opcode::iLALU_LD, RegType::LREG_R5, RegType::LREG_R6, RegType::LREG_R7, RegType::LREG_R8)
                      ,pc
                      ,addr
                      ,0
                      ,true);*/
       /*auto *alu_dinst= Dinst::create(Instruction(Opcode::iCALU_FPALU, RegType::LREG_FP3, RegType::LREG_FP4, RegType::LREG_FP7, RegType::LREG_FP9)
                      ,pc
                      ,0
                      ,0
                      ,true);*/
        /* auto  *alu_dinst = Dinst::create(Instruction(Opcode::iBALU_LBRANCH, RegType::LREG_R31, RegType::LREG_R30, RegType::LREG_R29, RegType::LREG_R31)
                                    ,pc
                                    ,0
                                    ,0
                                    ,true);*/
         /*auto  *alu_dinst = Dinst::create(Instruction(Opcode::iBALU_LBRANCH, RegType::LREG_R17, RegType::LREG_R14, RegType::LREG_R19, RegType::LREG_R25)
                                    ,pc
                                    ,addr
                                    ,0
                                    ,true);*/
       
      /*Dinst *alu_dinst;                             
       if(rand() & 1){
          alu_dinst = Dinst::create(Instruction(Opcode::iAALU, RegType::LREG_R31, RegType::LREG_R30, RegType::LREG_R29, RegType::LREG_R31)
                                    ,pc
                                    ,0
                                    ,0
                                    ,true);
      } else if  (rand() & 1){
           Addr_t addr    = random_addr_Gen();
           alu_dinst= Dinst::create(Instruction(Opcode::iSALU_ST, RegType::LREG_R5, RegType::LREG_R6, RegType::LREG_R7, RegType::LREG_R8)
                      ,pc
                      ,addr
                      ,0
                      ,true);
       } else if (rand() & 1){
           
           Addr_t addr    = random_addr_Gen();
           alu_dinst= Dinst::create(Instruction(Opcode::iLALU_LD, RegType::LREG_R5, RegType::LREG_R6, RegType::LREG_R7, RegType::LREG_R8)
                      ,pc
                      ,addr
                      ,0
                      ,true);
       } else if(rand() & 1){
           alu_dinst= Dinst::create(Instruction(Opcode::iCALU_FPALU, RegType::LREG_FP3, RegType::LREG_FP4, RegType::LREG_FP7, RegType::LREG_FP9)
                      ,pc
                      ,0
                      ,0
                      ,true);
       } else if(rand() & 1){
           alu_dinst = Dinst::create(Instruction(Opcode::iBALU_LBRANCH, RegType::LREG_R31, RegType::LREG_R30, RegType::LREG_R29, RegType::LREG_R31)
                                    ,pc
                                    ,0
                                    ,0
                                    ,true);
       } else {
           alu_dinst = Dinst::create(Instruction(Opcode::iAALU, RegType::LREG_R3, RegType::LREG_R3, RegType::LREG_R3, RegType::LREG_R3)
                                    ,pc
                                    ,0
                                    ,0
                                    ,true);
       }*/
       Dinst *alu_dinst;                             
       if(rand() & 1){
          alu_dinst = Dinst::create(Instruction(Opcode::iAALU, RegType::LREG_R11, RegType::LREG_R31, RegType::LREG_R0, RegType::LREG_R21)
                                    ,pc
                                    ,0
                                    ,0
                                    ,true);
       } else if(rand() & 1) {

           Addr_t addr    = random_addr_Gen();
           //alu_dinst= Dinst::create(Instruction(Opcode::iLALU_LD, RegType::LREG_R5, RegType::LREG_R6, RegType::LREG_R7, RegType::LREG_R0)
           alu_dinst= Dinst::create(Instruction(Opcode::iLALU_LD, RegType::LREG_R5, RegType::LREG_R6, RegType::LREG_R0, RegType::LREG_R7)
                      ,pc
                      ,addr
                      ,0
                      ,true);
       //} else if(rand() & 1){
         } else if(rand() & 1){
           alu_dinst= Dinst::create(Instruction(Opcode::iCALU_FPALU, RegType::LREG_FP3, RegType::LREG_FP4, RegType::LREG_FP7, RegType::LREG_FP0)
                      ,pc
                      ,0
                      ,0
                      ,true);
         } else if(rand() & 1) {
           Addr_t addr    = random_addr_Gen();
           //alu_dinst= Dinst::create(Instruction(Opcode::iLALU_LD, RegType::LREG_R5, RegType::LREG_R6, RegType::LREG_R7, RegType::LREG_R0)
           alu_dinst= Dinst::create(Instruction(Opcode::iSALU_ST, RegType::LREG_R5, RegType::LREG_R6, RegType::LREG_R0, RegType::LREG_R7)
                      ,pc
                      ,addr
                      ,0
                      ,true);
      /* } else if(rand() & 1){
           //Addr_t addr    = random_addr_Gen();
           alu_dinst = Dinst::create(Instruction(Opcode::iBALU_LBRANCH, RegType::LREG_R17, RegType::LREG_R14, RegType::LREG_R0, RegType::LREG_R25)
                                    ,pc
                                    ,0
                                    ,0
                                    ,true);*/

       } else {
           alu_dinst = Dinst::create(Instruction(Opcode::iAALU, RegType::LREG_R13, RegType::LREG_R13, RegType::LREG_R0, RegType::LREG_R3)
                                    ,pc
                                    ,0
                                    ,0
                                    ,true);
       }
   
         if(alu_dinst)
           std::cout<<std::endl<< "gProcessor::Yahoo!!Transient  Inst Created Opcode is "<<alu_dinst->getInst()->getOpcodeName()<<std::endl;
         alu_dinst->setTransient();
         if (bucket) {
           //alu_dinst->setFetchTime();
           bucket->push(alu_dinst);
           Tracer::stage(alu_dinst, "TIF");
           //spaceInInstQueue -= bucket->size();
           //pipeQ.pipeLine.readyItem(bucket);//must bucket-> markedfetch()
           printf("gProcessor::Yahoo!!! Bucket Inst Created %ld and bucket size is %lu\n", 
             alu_dinst->getID(), bucket->size());
         }
         i++;
         pc = pc + 4;
       }
      pipeQ.pipeLine.readyItem(bucket);//must bucket-> markedfetch() after loop
}




/*GProcessor::GProcessor(std::shared_ptr<Gmemory_system> gm, Hartid_t i)
    : Simu_base(gm, i)
    , FetchWidth(Config::get_integer("soc", "core", i, "fetch_width"))
    , IssueWidth(Config::get_integer("soc", "core", i, "issue_width"))
    , RetireWidth(Config::get_integer("soc", "core", i, "retire_width"))
    , RealisticWidth(RetireWidth < IssueWidth ? RetireWidth : IssueWidth)
    , InstQueueSize(Config::get_integer("soc", "core", i, "instq_size"))
    , MaxROBSize(Config::get_integer("soc", "core", i, "rob_size", 4))
    , memorySystem(gm)
    , rROB(Config::get_integer("soc", "core", i, "rob_size"))
    , ROB(MaxROBSize)
    , avgFetchWidth(fmt::format("P({})_avgFetchWidth", i))
    , rrobUsed(fmt::format("({})_rrobUsed", i))  // avg
    , robUsed(fmt::format("({})_robUsed", i))    // avg
    , nReplayInst(fmt::format("({})_nReplayInst", i))
    , nCommitted(fmt::format("({}):nCommitted", i))  // Should be the same as robUsed - replayed
    , noFetch(fmt::format("({}):noFetch", i))
    , noFetch2(fmt::format("({}):noFetch2", i))
    , pipeQ(i) {
  smt_size = Config::get_integer("soc", "core", i, "smt", 1, 32);

  lastReplay = 0;

  nStall[SmallWinStall]     = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nSmallWinStall", i));
  nStall[SmallROBStall]     = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nSmallROBStall", i));
  nStall[SmallREGStall]     = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nSmallREGStall", i));
  nStall[DivergeStall]      = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nDivergeStall", i));
  nStall[OutsLoadsStall]    = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nOutsLoadsStall", i));
  nStall[OutsStoresStall]   = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nOutsStoresStall", i));
  nStall[OutsBranchesStall] = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nOutsBranchesStall", i));
  nStall[ReplaysStall]      = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nReplaysStall", i));
  nStall[SyscallStall]      = std::make_unique<Stats_cntr>(fmt::format("P({})_ExeEngine:nSyscallStall", i));

  I(ROB.size() == 0);

  buildInstStats("ExeEngine");

#ifdef WAVESNAP_EN
  snap = std::make_unique<Wavesnap>();
#endif

  scb        = std::make_shared<Store_buffer>(i, gm);
  storeset   = std::make_shared<StoreSet>(i);
  prefetcher = std::make_shared<Prefetcher>(gm->getDL1(), i);

  use_stats = false;

  smt_fetch.fe.emplace_back(std::make_unique<FetchEngine>(i, gm));

  for (auto n = 1u; n < smt_size; ++n) {
    smt_fetch.fe.emplace_back(std::make_unique<FetchEngine>(i, gm, smt_fetch.fe[0]->ref_bpred()));
  }

  spaceInInstQueue = InstQueueSize;

  busy = false;
}

GProcessor::~GProcessor() {}

void GProcessor::buildInstStats(const std::string &txt) {
  for (const auto t : Opcodes) {
    nInst[t] = std::make_unique<Stats_cntr>(fmt::format("P({})_{}_{}:n", hid, txt, t));
  }
}
*/
int32_t GProcessor::issue() {
  int32_t i = 0;  // Instructions executed counter

  printf("\ngProc::Issue Entering issue!!! \n");
  I(!pipeQ.instQueue.empty());

  do {
    IBucket *bucket = pipeQ.instQueue.top();
    do {
      I(!bucket->empty());
      if (i >= IssueWidth) {
        printf("gProc::Issue  Sorry Wrong!!! ::i>= issuewidth is %d %d\n",i, IssueWidth);
        return i;
      }

      I(!bucket->empty());
      
      Dinst *dinst = bucket->top();
      if(dinst->isTransient())
        printf("gProc::Issue Transient  gets from bucketsize %ld \n",bucket->size());
      else 
        printf("gProc::Issue  bucketsize %ld \n",bucket->size());

      printf("pProcessor::Issue Inst is %ld \n", dinst->getID());

      std::cout<< "gProcessor:: issueYahoo!!!Inst issued Opcode"<< dinst->getInst()->getOpcodeName()<<std::endl;
      StallCause c = add_inst(dinst);
      if (c != NoStall) {
        if (i < RealisticWidth) {
          nStall[c]->add(RealisticWidth - i, dinst->has_stats());
          printf("gProc::Issue  Sorry Wrong!!! ::i<RealWidthis %d %d\n",i, RealisticWidth);
        }
          printf("gProc::Issue  Sorry only  stall added  %d %d\n",i, RealisticWidth);
        return i;
      }
      dinst->setGProc(this);
      i++;

      bucket->pop();

    } while (!bucket->empty());

    pipeQ.pipeLine.doneItem(bucket);
    pipeQ.instQueue.pop();
  } while (!pipeQ.instQueue.empty());

  printf("\ngProc::issue Leaving Correctly\n");
  return i;
}

bool GProcessor::decode_stage() {
  printf("gProc::decode Entering \n");
  if (!ROB.empty()) {
    use_stats = ROB.top()->has_stats();
  }

  bool new_clock = adjust_clock(use_stats);
  if (!new_clock) {
    return true;
  }

  // ID Stage (insert to instQueue)
  if (spaceInInstQueue >= FetchWidth) {
    IBucket *bucket = pipeQ.pipeLine.nextItem();
    if (bucket) {
      I(!bucket->empty());
      printf("\ngProc::decode top Instid is %ld and decode bucketsize is %ld\n", 
          bucket->top()->getID(), bucket->size());
      std::cout<< "gProcessor:: decode Yahoo!!!Inst Opcode "<< bucket->top()->getInst()->getOpcodeName()<<std::endl;
      spaceInInstQueue -= bucket->size();
      pipeQ.instQueue.push(bucket);


    } else {
      noFetch2.inc(use_stats);
      printf("\ngProc::decode No fetch: Bucket Empty!!!");
    }
  } else {
    noFetch.inc(use_stats);
    printf("\ngProc::decode No fetch: spaceInInstQueue < FetchWidth");
  }

  //IBucket *bucket = pipeQ.instQueue.top();
  //pipeQ.instQueue.push(bucket);
  //printf("\ngProc::decode Leaving with pipeQ.InstQ.bucket size %ld\n", bucket->size());
  printf("\ngProc::decode Leaving \n");
  return false;
}
