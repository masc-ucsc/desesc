// See LICENSE for details.

#include "gprocessor.hpp"

#include <sys/time.h>
#include <unistd.h>

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
        printf("gprocessor::fetch on branchmiss{}%lx\n",ifid->get_miss_dinst()->getAddr());
        printf("gprocessor::fetch on branchmiss + 4 {}%lx\n",pc);
       //printf("gprocessor::fetch on branchmiss{}%lx\n", ifid->getMissDinst()->getAddr());
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
      avgFetchWidth.sample(bucket->size(), bucket->top()->has_stats());
      busy = true;
    }
  }
}
void GProcessor::flush_transient_inst_on_fetch_ready() {
 
  pipeQ.pipeLine.flush_transient_inst_from_buffer();
  flush_transient_inst_from_inst_queue();
  flush_rob();
}
void GProcessor::flush_rob() {
//try the for loop scan
  while(!ROB.empty()) {
    //auto *dinst = ROB.top();
    printf("GPROCCESOR::ROB FLUSHING  ENTERING \n");  
    auto *dinst = ROB.end_data();
    //if (!dinst->isTransient())
      //break;
    printf("GPROCCESOR::ROB FLUSHING :: instID %lx\n", dinst->getID());  
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
         printf("GPROCCESOR::ROB FLUSHING destroying_ROB transient instID %lx\n", dinst->getID());  
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



void GProcessor::flush_transient_inst_from_inst_queue() {
    
    //printf("PipeQ::Entering ::instqueue size is %lu\n",pipeQ.instQueue.size()); 
    
    while(!pipeQ.instQueue.empty()) {
    
    //printf("PipeQ::flush::!buffer.empty () buffer size inside is %lu\n",pipeQ.instQueue.size()); 
    auto *bucket = pipeQ.instQueue.top();
    if (bucket) {
      while(!bucket->empty()) {
        auto *dinst = bucket->top();
        printf("Gprocess::flush_inst_queue::instqueue.size is %lu and instID %lx\n",bucket->size(), 
            dinst->getID()); 
        bucket->pop();
        //I(dinst->isTransient());
        if (dinst->isTransient() && !dinst->is_present_in_rob()) {
         printf("Gprocess::flush_inst_queue::instqueue.size is %lu and  destroying transient instID %lx\n",bucket->size(), 
         dinst->getID());  
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





void GProcessor:: add_inst_transient_on_branch_miss(IBucket *bucket, Addr_t pc) {
       int i=0;
       while (i< FetchWidth) {
         //printf("gProcessor:: Entering transient Inst\n");
         auto  *alu_dinst = Dinst::create(Instruction(Opcode::iAALU, RegType::LREG_R3, RegType::LREG_R3, RegType::LREG_R3, RegType::LREG_R3)
                                    ,pc
                                    ,0
                                    ,0
                                    ,true);
         
         if(alu_dinst)
           std::cout<<std::endl<< "gProcessor::Yahoo!!Transient  Inst Created Opcode is "<<alu_dinst->getInst()->getOpcodeName()<<std::endl;
         alu_dinst->setTransient();
         if (bucket) {
           //alu_dinst->setFetchTime();
           bucket->push(alu_dinst);
           Tracer::stage(alu_dinst, "IF");
           //spaceInInstQueue -= bucket->size();
           //pipeQ.pipeLine.readyItem(bucket);//must bucket-> markedfetch()
           printf("gProcessor::Yahoo!!! Bucket Inst Created %lx and bucket size is %lu\n", 
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
      printf("\ngProc::decode top Instid is %lx and decode bucketsize is %ld\n", 
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
