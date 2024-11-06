// BISMILLAH HIR RAHMAR NIR RAHIM
//  See LICENSE for details.
#include "gprocessor.hpp"

#include <sys/time.h>
#include <unistd.h>

#include <string>

#include "addresspredictor.hpp"
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
    , do_random_transients(Config::get_bool("soc", "core", i, "do_random_transients"))
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
  I(eint);
  I(is_power_up());

  if (spaceInInstQueue < FetchWidth) {
    printf("gprocessor::No_new_fetch:: spaceInInstQueue < FetchWidth!!!\n");
    printf("gprocessor::spaceInInstQueue is %d and FetchWidth is %d \n", spaceInInstQueue, FetchWidth);
    return;
  }

  auto ifid = smt_fetch.fetch_next();
  /*<<<<<<< HEAD
   //must be before *bucket
    //SpecPower_turns_off
    if (ifid->isBlocked()) {
      // fmt::print("fetch on {}\n", ifid->getMissDinst()->getID());
       return;
    }

  =======*/
  // must be before *bucket
  if (ifid->isBlocked() && !do_random_transients) {
    return;
  }
  //>>>>>>> upstream/main

  auto     smt_hid = hid;  // FIXME: do SMT fetch
  IBucket *bucket  = pipeQ.pipeLine.newItem();

  //<<<<<<< HEAD
  if (ifid->isBlocked()) {
    Addr_t pc = ifid->getMissDinst()->getAddr() + 4;
    // printf("gprocessor::fetch on branchmiss{}%ld\n", ifid->get_miss_dinst()->getAddr());
    // printf("gprocessor::fetch on branchmiss + 4 {}%ld\n", pc);
    // printf("gprocessor::fetch on branchmiss{}%ld\n", ifid->getMissDinst()->getAddr());

    /*auto *dinst_br= ifid->getMissDinst();
    if(dinst_br->getCluster()->get_reg_pool() >= dinst_br->getCluster()->get_nregs()-7) {
      return;
    }*/
    if (bucket) {
      add_inst_transient_on_branch_miss(bucket, pc);
    }

    return;
  }
  // enable only when !ifid->isblocked() to flush transient before every new fetch
  // after a branch miss-prediction resolved at execute stage
  // if(ifid->is_fetch_next_ready) {
  // flush_transient_inst_on_fetch_ready();
  // }

  // FIXME::if(!ifid->isBlocked() && do_random_transients)
  /*if(!ifid->isBlocked() && do_random_transients) {
   flush_transient_inst_on_fetch_ready();
  }*/
  /*if(!ifid->isBlocked() && do_random_transients && ifid->get_is_fetch_next_ready()) {
   flush_transient_inst_on_fetch_ready();
   ifid->reset_is_fetch_next_ready();
  }*/

  if (bucket) {
    ifid->fetch(bucket, eint, smt_hid, this);
    if (!bucket->empty()) {
      // printf("gprocessor::fetch:: completed bucket size is %ld\n", bucket->size());
      avgFetchWidth.sample(bucket->size(), bucket->top()->has_stats());
      busy = true;
    }
  }
}
void GProcessor::flush_transient_inst_on_fetch_ready_delay() {
  spaceInInstQueue = InstQueueSize;
  // printf("gprocessor::flush_transient_inst_queue on before new fetch!!!\n");
  flush_transient_inst_from_inst_queue();
  // printf("gprocessor::flush_transient_buffer on before new fetch!!!\n");
  // pipeQ.pipeLine.flush_transient_inst_from_buffer();
  // printf("gprocessor::flush_transient_rob on before new fetch!!!\n");
  flush_transient_from_rob();
}
void GProcessor::flush_transient_inst_on_fetch_ready() {
  // printf("gprocessor::flush_transient_pipeline_instq_rob on before new fetch!!!\n");

  spaceInInstQueue = InstQueueSize;
  // printf("gprocessor::flush_transient_inst_queue on before new fetch!!!\n");
  flush_transient_inst_from_inst_queue();
  // printf("gprocessor::flush_transient_buffer on before new fetch!!!\n");
  pipeQ.pipeLine.flush_transient_inst_from_buffer();
  // printf("gprocessor::flush_transient_rob on before new fetch!!!\n");
  flush_transient_from_rob();
}
void GProcessor::dump_rob()
// {{{1 Dump rob statistics
{
  uint32_t size = ROB.size();
  // printf("dump_ROB: ROB_size:%d\n", size);

  for (uint32_t i = 0; i < size; i++) {
    uint32_t pos = ROB.getIDFromTop(i);

    Dinst *dinst = ROB.getData(pos);
    if (dinst->isTransient()) {
      dinst->clearRATEntry();
    }

    dinst->dump("");
  }
}

void GProcessor::flush_transient_from_rob() {
  // try the for loop scan
  // printf("gprocessor::flush_transient_rob on before new fetch!!!\n");
  while (!ROB.empty()) {
    auto *dinst = ROB.end_data();
    // makes sure isExecuted in preretire()
    // printf("gprocessor::flush_transient_rob ROB size is %ld!!!\n", ROB.size());

    if (!dinst->isTransient()) {
      // printf("GPROCCESOR::flush_Rob ::NON TRANSIENT  instID %ld\n", dinst->getID());
      ROB.push_pipe_in_cluster(dinst);
      ROB.pop_from_back();
      continue;
    }
    /*if(dinst->mark_destroy_transient()) {
      continue;
    }*/

    // printf("GPROCCESOR::flush_Rob ::Entering::TRANSIENT  instID %ld\n", dinst->getID());
    dinst->clearRATEntry();
    // dinst->mark_to_be_destroyed_transient();
    /*if (dinst->getCluster()->get_reg_pool() >= dinst->getCluster()->get_nregs() - 2) {
      break;
    }*lima_may*/
    /*if (dinst->getCluster()->get_reg_pool() >= dinst->getCluster()->get_nregs() - 7) {
      printf("GPROCCESOR::flush_Rob ::TRANSIENT get_reg_pool() >= dinst->getCluster()->get_nregs() - 7:: instID %ld\n",
    dinst->getID()); dump_rob(); break; }lima_june24*/

    /*limasep if (dinst->getCluster()->get_window_size() == dinst->getCluster()->get_window_maxsize()) {
       printf("GPROCCESOR::flush_Rob :::>get_window_size() == dinst->getCluster()->get_window_maxsize( instID %ld\n",
     dinst->getID()); dump_rob(); break;
     }*/
    /*if (dinst->hasDeps() || dinst->hasPending()) {
      ROB.push_pipe_in_cluster(dinst);
      ROB.pop_from_back();
      continue;
    }*/

    // printf("GPROCCESOR::flush_Rob ::Entering  instID %ld\n", dinst->getID());
    // if(dinst)
    dinst->mark_destroy_transient();
    if (!dinst->isRetired() && dinst->isExecuted()) {
      // dinst->clearRATEntry();
      while (dinst->hasPending()) {
        // printf("GPROCCESOR::flush_Rob :: isRetired() Pending for instID %ld at @Clockcycle %ld\n", dinst->getID(), globalClock);
        Dinst *dstReady = dinst->getNextPending();
        I(dstReady->isTransient());
      }
      bool hasDest = (dinst->getInst()->hasDstRegister());
      if (hasDest && !dinst->is_try_flush_transient()) {
        // printf("GPROCCESOR::flush_Rob :: isRetired()  regpool++ destroying for instID %ld at @Clockcycle %ld\n",
        // dinst->getID(),
        // globalClock);
        dinst->getCluster()->add_reg_pool();
      }
      dinst->clearRATEntry();
      dinst->getCluster()->try_flushed(dinst);
      try_flush(dinst);
      // dinst->getCluster()->delEntry();:: happens automatically in cluster::ExecutedCluster ::delEntry()
      dinst->destroyTransientInst();
    } else if (dinst->isExecuting() || dinst->isIssued()) {
      if (dinst->is_try_flush_transient()) {
        ROB.push_pipe_in_cluster(dinst);
        ROB.pop_from_back();
        continue;
      }
      dinst->mark_flush_transient();
      dinst->clearRATEntry();
      dinst->getCluster()->try_flushed(dinst);
      // limasep2024dinst->mark_del_entry();
      dinst->getCluster()->del_entry_flush(dinst);
      // printf(
      //     "GPROCCESOR::flush_Rob :: isExecuting || isIssued()  :: not destroyed::windowsize is %d: for instID %ld at @Clockcycle
      //     "
      //     "%ld\n",
      //     dinst->getCluster()->get_window_size(),
      //     dinst->getID(),
      //     globalClock);
      // lima_june24
      bool hasDest = (dinst->getInst()->hasDstRegister());
      if (hasDest) {
        // printf("GPROCCESOR::flush_Rob :: isExecuting || isIssued()  regpool++ destroying for instID %ld at @Clockcycle %ld\n",
        //        dinst->getID(),
        //        globalClock);
        dinst->getCluster()->add_reg_pool();
        dinst->mark_try_flush_transient();
        // dinst->getCluster()->delEntry();
      }
      // lima_june24*/

      // printf("GPROCCESOR::flush_Rob ::mark flush:: isExecuting || isIssued instID %ld\n", dinst->getID());
      ROB.push_pipe_in_cluster(dinst);
    } else if (dinst->isRenamed()) {
      if (dinst->is_try_flush_transient()) {
        ROB.push_pipe_in_cluster(dinst);
        ROB.pop_from_back();
        continue;
      }
      dinst->clearRATEntry();
      dinst->getCluster()->try_flushed(dinst);
      // lima2024sepdinst->mark_del_entry();
      dinst->getCluster()->del_entry_flush(dinst);
      // printf("GPROCCESOR::flush_Rob :: isRenamed :: not destroyed::After windowsize is %d: for instID %ld at @Clockcycle %ld\n",
      //        dinst->getCluster()->get_window_size(),
      //        dinst->getID(),
      //        globalClock);
      // lima_june
      bool hasDest = (dinst->getInst()->hasDstRegister());
      if (hasDest) {
        // printf("GPROCCESOR::flush_Rob :: isRenamed  regpool++ destroying for instID %ld at @Clockcycle %ld\n",
        //        dinst->getID(),
        //        globalClock);
        dinst->getCluster()->add_reg_pool();
        dinst->mark_try_flush_transient();
      }
      // lima_june*/
      ROB.push_pipe_in_cluster(dinst);
    }

    // printf("GPROCCESOR::flush_Rob :: Poping from ROB.pop_from_back()  instID %ld at @Clockcycle %ld\n",
    //        dinst->getID(),
    //        globalClock);
    ROB.pop_from_back();
  }

  /*} else if (dinst->isRenamed()) {
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
        printf("GPROCCESOR::flush_Rob : isRenamed markexecuted transient  instID %ld\n", dinst->getID());
        dinst->markExecutedTransient();
        //dinst->clearRATEntry();
        printf("GPROCCESOR::flush_Rob : clear RAT entry instID %ld\n", dinst->getID());
=======
    ///////startshere///if (!dinst->isRetired() && dinst->isExecuted()) {
      // dinst->clearRATEntry();
      while (dinst->hasPending()) {
        Dinst *dstReady = dinst->getNextPending();
        I(dstReady->isTransient());
      }
      bool hasDest = (dinst->getInst()->hasDstRegister());
      if (hasDest) {
        dinst->getCluster()->add_reg_pool();
      }
      dinst->destroyTransientInst();
    } else if (dinst->isExecuting() || dinst->isIssued()) {
      dinst->mark_flush_transient();
      while (dinst->hasPending()) {
        Dinst *dstReady = dinst->getNextPending();
        I(dstReady->isTransient());
      }

      ROB.push_pipe_in_cluster(dinst);
    } else if (dinst->isRenamed()) {
      // Rename :RN
      if (dinst->getCluster()->get_reg_pool() >= dinst->getCluster()->get_nregs() - 3) {
>>>>>>> upstream/main
//ends here////////////////////
        while (dinst->hasPending()) {
          Dinst *dstReady = dinst->getNextPending();
          I(dstReady->isTransient());
        }
        break;
      }
      dinst->markExecutedTransient();
      dinst->clearRATEntry();
      while (dinst->hasPending()) {
        Dinst *dstReady = dinst->getNextPending();
        I(dstReady->isTransient());
      }

<<<<<<< HEAD
        dinst->clearRATEntry();

       //printf("GPROCCESOR::flush_Rob : isRenamed current instID %ld and getParentScr1 ID is: %ld and and getParentScr2 ID is :
%ld\n",
         //   dinst->getID(),dinst->getParenmSrc1()->getID(),dinst->getParentSrc1()->getID() );

       printf("GPROCCESOR::flush_Rob : isRenamed current instID %ld\n", dinst->getID());




        dinst->markExecutedTransient();
        dinst->getCluster()->delEntry();
        if(!dinst->hasDeps()){
        dinst->destroyTransientInst();
        } else {
        dinst->mark_to_be_destroyed_transient();
        }
    }//if_renamed*/

  /*=======
        bool hasDest = (dinst->getInst()->hasDstRegister());
        if (hasDest) {
          dinst->getCluster()->add_reg_pool();
        }

        dinst->getCluster()->delEntry();
        dinst->destroyTransientInst();
      }

  >>>>>>> upstream/main*/
  // ROB.pop_from_back();
  // }
  while (!ROB.empty_pipe_in_cluster()) {
    auto *dinst = ROB.back_pipe_in_cluster();  // get last element from vector:back()

    //<<<<<<< HEAD

    if (dinst->is_flush_transient() && dinst->isExecuted() && !dinst->hasDeps() && !dinst->hasPending()) {
      if (dinst->getCluster()->get_window_size() < dinst->getCluster()->get_window_maxsize() - 1) {
        bool hasDest = (dinst->getInst()->hasDstRegister());
        if (hasDest && !dinst->is_try_flush_transient()) {
          dinst->getCluster()->add_reg_pool();
        }
        // if (!dinst->is_try_flush_transient()) {
        //   // printf("GPROCCESOR::flush_Rob :: ROB_back_in:: not destroyed::windowsize is %d: for instID %ld at @Clockcycle
        //   %ld\n",
        //   //        dinst->getCluster()->get_window_size(),
        //   //        dinst->getID(),
        //   //        globalClock);
        //   // dinst->getCluster()->delEntry();
        //   // printf("GPROCCESOR::flush_Rob :: ROB_back_in:: not destroyed::windowsize++ is %d: for instID %ld at @Clockcycle
        //   %ld\n",
        //   //        dinst->getCluster()->get_window_size(),
        //   //        dinst->getID(),
        //   //        globalClock);
        // }

        dinst->markExecutedTransient();
        dinst->clearRATEntry();
        dinst->getCluster()->try_flushed(dinst);
        try_flush(dinst);
        // 2024_sep//
        if (!dinst->is_del_entry()) {
          // limasep2024dinst->mark_del_entry();
          dinst->getCluster()->del_entry_flush(dinst);
        }
        // dinst->getCluster()->delEntry();
        dinst->destroyTransientInst();
      }
    } else {
      ROB.push(dinst);  // push in the end of ROB
      if (!dinst->is_del_entry() && dinst->isTransient()) {
        // limaspe2024dinst->mark_del_entry();
        dinst->getCluster()->del_entry_flush(dinst);
      }
      // added lima sep 2024
    }
    ROB.pop_pipe_in_cluster();  // pop last element from buffer_ROB
  }
  printf("gprocessor::flush_transient_rob Leaving before new fetch!!!\n");
}

//<<<<<<< HEAD

//=======
/*void GProcessor::flush_transient_from_rob() {
//try the for loop scan
  while(!ROB.empty()) {
    //auto *dinst = ROB.top();
    auto *dinst = ROB.end_data();
    //if (!dinst->isTransient())
      //break;
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
//>>>>>>> upstream/main
/*
void GProcessor::flush_transient_inst_from_inst_queue() {
  printf("gprocessor::flush_transient_inst_queue Entering before new fetch!!!\n");
  while (!pipeQ.instQueue.empty()) {
    auto *bucket = pipeQ.instQueue.top();
    if (bucket) {
      while (!bucket->empty()) {
        auto *dinst = bucket->top();
        bucket->pop();
        // I(dinst->isTransient());
        if (dinst->isTransient() && !dinst->is_present_in_rob()) {
//<<<<<<< HEAD
         printf("Gprocess::flush_inst_queue::instqueue.size is %lu and  destroying transient instID %ld\n",bucket->size(),
         dinst->getID());
        // noneed:dinst->clearRATEntry();
         dinst->destroyTransientInst();
//=======
         // dinst->destroyTransientInst();
//>>>>>>> upstream/main
        }
         else if (dinst->isTransient()) {
            printf("Gprocessor::InstQflush:: NOT destroying NON transient bucket ::::size is %lu and instID is
%ld\n",bucket->size(),dinst->getID());
      }
      }
      if (bucket->empty()) {  // FIXME
        I(bucket->empty());
        pipeQ.pipeLine.doneItem(bucket);
      }
    }
    pipeQ.instQueue.pop();
  }
  printf("gprocessor::flush_transient_inst_queue Leaving  before new fetch!!!\n");
}
//<<<<<<< HEADDownward
*/
void GProcessor::flush_transient_inst_from_inst_queue() {
  printf("gprocessor::flush_transient_inst_queue Entering before new fetch!!!\n");
  while (!pipeQ.instQueue.empty()) {
    auto *bucket = pipeQ.instQueue.end_data();
    if (bucket) {
      while (!bucket->empty()) {
        // auto *dinst = bucket->top();
        auto *dinst = bucket->end_data();
        // bucket->pop();
        //  I(dinst->isTransient());
        if (dinst->isTransient() && !dinst->is_present_in_rob()) {
          // printf("Gprocess::flush_inst_queue::instqueue.size is %lu and  destroying transient instID %ld\n",
          //        bucket->size(),
          //        dinst->getID());
          // noneed:dinst->clearRATEntry();
          dinst->destroyTransientInst();
          // bucket->pop();
          bucket->pop_from_back();
          //=======
          // dinst->destroyTransientInst();
        } else if (dinst->isTransient()) {
          // printf("Gprocessor::InstQflush:: NOT destroying NON transient bucket ::::size is %lu and instID is %ld\n",
          //        bucket->size(),
          //        dinst->getID());
        }
      }
      if (bucket->empty()) {  // FIXME
        I(bucket->empty());
        pipeQ.pipeLine.doneItem(bucket);
      }
    }
    pipeQ.instQueue.pop_from_back();
  }
  // printf("gprocessor::flush_transient_inst_queue Leaving  before new fetch!!!\n");
}
//<<<<<<< HEADDownward

/*
Addr_t GProcessor::random_addr_gen(){
      Addr_t addr = 0x200;
      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_int_distribution<> dis(1, 100);
      int randomNumber = dis(gen);
      return addr+(uint64_t)randomNumber;
}

uint64_t GProcessor::random_reg_gen( bool reg){
*/
Addr_t GProcessor::random_addr_gen() {
  Addr_t                          addr = 0x200;
  std::random_device              rd;
  std::mt19937                    gen(rd());
  std::uniform_int_distribution<> dis(1, 100);
  int                             randomNumber = dis(gen);
  return addr + (uint64_t)randomNumber;
}

uint64_t GProcessor::random_reg_gen(bool reg) {
  // std::random_device rd;
  //  std::mt19937 gen(rd());
  static std::mt19937 gen(55);

  if (reg) {
    std::uniform_int_distribution<> dis_reg(1, 31);
    int                             randomNumber_reg = dis_reg(gen);
    return (uint64_t)randomNumber_reg;

  } else {
    std::uniform_int_distribution<> dis_fp(33, 63);
    int                             randomNumber_fp = dis_fp(gen);
    return (uint64_t)randomNumber_fp;
  }
}

void GProcessor::add_inst_transient_on_branch_miss(IBucket *bucket, Addr_t pc) {
  int i = 0;
  // string a="LREG_R31";
  // while (i< FetchWidth/2) {
  while (i < 2) {
    // while (i< 3) {
    // printf("gProcessor:: Entering transient Inst\n");
    // auto  *alu_dinst = Dinst::create(Instruction(Opcode::iAALU, RegType::LREG_R3, RegType::LREG_R3, RegType::LREG_R3,
    // RegType::LREG_R3)
    /*auto  *alu_dinst = Dinst::create(Instruction(Opcode::iAALU, RegType::LREG_R31, RegType::LREG_R30, RegType::LREG_R29,
       RegType::LREG_R31) ,pc ,0 ,0 ,true);*/

    // Addr_t addr    = random_addr_Gen();
    /*auto *alu_dinst= Dinst::create(Instruction(Opcode::iSALU_ST, RegType::LREG_R5, RegType::LREG_R6, RegType::LREG_R7,
       RegType::LREG_R8) ,pc ,addr ,0 ,true);*/

    /*auto *alu_dinst= Dinst::create(Instruction(Opcode::iLALU_LD, RegType::LREG_R5, RegType::LREG_R6, RegType::LREG_R7,
       RegType::LREG_R8) ,pc ,addr ,0 ,true);*/
    /*auto *alu_dinst= Dinst::create(Instruction(Opcode::iCALU_FPALU, RegType::LREG_FP3, RegType::LREG_FP4, RegType::LREG_FP7,
       RegType::LREG_FP9) ,pc ,0 ,0 ,true);*/
    /* auto  *alu_dinst = Dinst::create(Instruction(Opcode::iBALU_LBRANCH, RegType::LREG_R31, RegType::LREG_R30, RegType::LREG_R29,
       RegType::LREG_R31) ,pc ,0 ,0 ,true);*/
    /*auto  *alu_dinst = Dinst::create(Instruction(Opcode::iBALU_LBRANCH, RegType::LREG_R17, RegType::LREG_R14, RegType::LREG_R19,
       RegType::LREG_R25) ,pc ,addr ,0 ,true);*/

    /*Dinst *alu_dinst;
     if(rand() & 1){
        alu_dinst = Dinst::create(Instruction(Opcode::iAALU, RegType::LREG_R31, RegType::LREG_R30, RegType::LREG_R29,
    RegType::LREG_R31) ,pc ,0 ,0 ,true); } else if  (rand() & 1){ Addr_t addr    = random_addr_Gen(); alu_dinst=
    Dinst::create(Instruction(Opcode::iSALU_ST, RegType::LREG_R5, RegType::LREG_R6, RegType::LREG_R7, RegType::LREG_R8) ,pc ,addr ,0
                    ,true);
     } else if (rand() & 1){

         Addr_t addr    = random_addr_Gen();
         alu_dinst= Dinst::create(Instruction(Opcode::iLALU_LD, RegType::LREG_R5, RegType::LREG_R6, RegType::LREG_R7,
    RegType::LREG_R8) ,pc ,addr ,0 ,true); } else if(rand() & 1){ alu_dinst= Dinst::create(Instruction(Opcode::iCALU_FPALU,
    RegType::LREG_FP3, RegType::LREG_FP4, RegType::LREG_FP7, RegType::LREG_FP9) ,pc ,0 ,0 ,true); } else if(rand() & 1){ alu_dinst =
    Dinst::create(Instruction(Opcode::iBALU_LBRANCH, RegType::LREG_R31, RegType::LREG_R30, RegType::LREG_R29, RegType::LREG_R31) ,pc
                                  ,0
                                  ,0
                                  ,true);
     } else {
         alu_dinst = Dinst::create(Instruction(Opcode::iAALU, RegType::LREG_R3, RegType::LREG_R3, RegType::LREG_R3,
    RegType::LREG_R3) ,pc ,0 ,0 ,true);
     }*/
    Dinst  *alu_dinst;
    RegType src1 = RegType::LREG_INVALID;
    RegType src2 = RegType::LREG_INVALID;
    RegType dst1 = RegType::LREG_INVALID;
    RegType dst2 = RegType::LREG_InvalidOutput;
    bool    reg  = true;

    src1 = (RegType)random_reg_gen(reg);
    src2 = (RegType)random_reg_gen(reg);
    dst1 = (RegType)random_reg_gen(reg);

    if (rand() & 1) {
      // alu_dinst = Dinst::create(Instruction(Opcode::iAALU, RegType::LREG_R11, RegType::LREG_R31, RegType::LREG_R0,
      // RegType::LREG_R21)
      alu_dinst = Dinst::create(Instruction(Opcode::iAALU, src1, src2, dst1, dst2), pc, 0, 0, true);
      /*} else if(rand() & 1) {

          Addr_t addr    = random_addr_gen();
          //src2    = RegType::LREG_NoDependence;

          //alu_dinst= Dinst::create(Instruction(Opcode::iLALU_LD, RegType::LREG_R5, RegType::LREG_R6, RegType::LREG_R0,
         RegType::LREG_R7) alu_dinst = Dinst::create(Instruction(Opcode::iLALU_LD, src1, src2, dst1, dst2) ,pc ,addr ,0 ,true);*/
      //} else if(rand() & 1){
    } else if (rand() & 1) {
      reg  = false;
      src1 = (RegType)random_reg_gen(reg);
      src2 = (RegType)random_reg_gen(reg);
      dst1 = (RegType)random_reg_gen(reg);

      // alu_dinst= Dinst::create(Instruction(Opcode::iCALU_FPALU, RegType::LREG_FP3, RegType::LREG_FP4, RegType::LREG_FP7,
      // RegType::LREG_FP0)
      // alu_dinst= Dinst::create(Instruction(Opcode::iCALU_FPALU, RegType::LREG_FP3, RegType::LREG_FP4, RegType::LREG_FP7,
      // RegType::LREG_InvalidOutput)
      alu_dinst = Dinst::create(Instruction(Opcode::iCALU_FPALU, src1, src2, dst1, dst2), pc, 0, 0, true);
      /*} else if(rand() & 1) {
           Addr_t addr    = random_addr_gen();
           //alu_dinst= Dinst::create(Instruction(Opcode::iLALU_LD, RegType::LREG_R5, RegType::LREG_R6, RegType::LREG_R7,
         RegType::LREG_R0)
           //goodalu_dinst= Dinst::create(Instruction(Opcode::iSALU_ST, RegType::LREG_R5, RegType::LREG_R6, RegType::LREG_R0,
         RegType::LREG_R7) dst1 = RegType::LREG_InvalidOutput; dst2 = RegType::LREG_InvalidOutput;

           alu_dinst = Dinst::create(Instruction(Opcode::iSALU_ST, src1, src2, dst1, dst2)
                      ,pc
                      ,addr
                      ,0
                      ,true);*/
    } else if (rand() & 1) {
      // alu_dinst = Dinst::create(Instruction(Opcode::iBALU_LBRANCH, RegType::LREG_R17, RegType::LREG_R14, RegType::LREG_R0,
      // RegType::LREG_InvalidOutput)
      alu_dinst = Dinst::create(Instruction(Opcode::iBALU_LBRANCH, src1, src2, dst1, dst2), pc, 0, 0, true);

    } else {
      alu_dinst = Dinst::create(
          Instruction(Opcode::iAALU, RegType::LREG_R13, RegType::LREG_R13, RegType::LREG_R0, RegType::LREG_InvalidOutput),
          pc,
          0,
          0,
          true);
    }

    // if (alu_dinst) {
    //   std::cout << std::endl
    //             << "gProcessor::Yahoo!!Transient  Inst Created Opcode is " << alu_dinst->getInst()->getOpcodeName() << std::endl;
    // }
    alu_dinst->setTransient();
    if (bucket) {
      // alu_dinst->setFetchTime();
      bucket->push(alu_dinst);
      Tracer::stage(alu_dinst, "TIF");
      // spaceInInstQueue -= bucket->size();
      // pipeQ.pipeLine.readyItem(bucket);//must bucket-> markedfetch()
      // printf("gProcessor::Yahoo!!! Bucket Inst Created %ld and bucket size is %lu\n", alu_dinst->getID(), bucket->size());
      // std::cout << "Gprocessor::add_inst::Transient :: inst asm is " << alu_dinst->getInst()->get_asm() << std::endl;
    }
    i++;
    pc = pc + 4;
  }
  pipeQ.pipeLine.readyItem(bucket);  // must bucket-> markedfetch() after loop
  //=======
  //>>>>>>> upstream/main
}

/*void GProcessor::add_inst_transient_on_branch_miss(IBucket *bucket, Addr_t pc) {
  int i = 0;
  // string a="LREG_R31";
  while (i < FetchWidth / 2) {
    // while (i< 3) {
    // auto  *alu_dinst = Dinst::create(Instruction(Opcode::iAALU, RegType::LREG_R3, RegType::LREG_R3, RegType::LREG_R3,
    // RegType::LREG_R3)
    auto *alu_dinst
        = Dinst::create(Instruction(Opcode::iAALU, RegType::LREG_R31, RegType::LREG_R30, RegType::LREG_R29, RegType::LREG_R31),
                        pc,
                        0,
                        0,
                        true);*/
/*auto  *alu_dinst = Dinst::create(Instruction(Opcode::iBALU_RBRANCH, RegType::LREG_R17, RegType::LREG_R14, RegType::LREG_R16,
   RegType::LREG_R19) ,pc ,0 ,0 ,true);*/

/*Addr_t addr    = 0x200;
auto *alu_dinst= Dinst::create(Instruction(Opcode::iSALU_ST, RegType::LREG_R5, RegType::LREG_R6, RegType::LREG_R7,
RegType::LREG_R8) ,pc ,addr ,0 ,true);*/

/*  alu_dinst->setTransient();
  if (bucket) {
    // alu_dinst->setFetchTime();
    bucket->push(alu_dinst);
    Tracer::stage(alu_dinst, "TIF");
    // spaceInInstQueue -= bucket->size();
    // pipeQ.pipeLine.readyItem(bucket);//must bucket-> markedfetch()
  }
  i++;
  pc = pc + 4;
}
pipeQ.pipeLine.readyItem(bucket);  // must bucket-> markedfetch() after loop
}
*/
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
/*void GProcessor:: add_non_flushed_non_transient_inst_back_to_inst_queue() {

  int32_t i = 0;  // Instructions executed counter
  IBucket *b = pipeQ.pipeLine.next_item_transient_adding_to_rob();
  I(!b->empty());
  do {
  Dinst *dinst = b->top();
  if (i >= IssueWidth) {
    return;
  }
  dinst->setGProc(this);
  StallCause c = add_inst(dinst);
  if (c != NoStall) {
    if (i < RealisticWidth) {
      nStall[c]->add(RealisticWidth - i, dinst->has_stats());
        }
        return ;
      }
      i++;
      b->pop();
    } while (!b->empty());

    pipeQ.pipeLine.doneItem(b);
}*/
/*
  if (bucket) {
           //alu_dinst->setFetchTime();
           bucket->push(dinst);
           //Tracer::stage(alu_dinst, "TIF");
           //spaceInInstQueue -= bucket->size();
           //pipeQ.pipeLine.readyItem(bucket);//must bucket-> markedfetch()
           printf("gProcessor::Yahoo!!! Bucket Inst Created %ld and bucket size is %lu\n",
             alu_dinst->getID(), bucket->size());
           std::cout<<"Gprocessor::add_inst::Transient :: inst asm is "<<alu_dinst->getInst()->get_asm()<<std::endl;

         }

      pipeQ.pipeLine.readyItem(bucket);
   }*/
// must bucket-> markedfetch() after loop
/*
      //=======
  //I(!pipeQ.instQueue.empty());
  IBucket *b = pipeQ.pipeLine.next_item_transient_adding_to_rob();
  if(b) {
  while (!b->empty()){
    if (b) {
        I(!b->empty());
        pipeQ.instQueue.push(b);
    }
    //b->pop();
    b = pipeQ.pipeLine.next_item_transient_adding_to_rob();
    //if(b)
  }
  }*/

int32_t GProcessor::issue() {
  int32_t i = 0;  // Instructions executed counter

  I(!pipeQ.instQueue.empty());

  do {
    IBucket *bucket = pipeQ.instQueue.top();
    do {
      I(!bucket->empty());
      if (i >= IssueWidth) {
        return i;
      }

      I(!bucket->empty());

      Dinst *dinst = bucket->top();
      // if (dinst->isTransient()) {
      //   printf("gProc::Issue Transient  gets from bucketsize %ld \n", bucket->size());
      // } else {
      //   printf("gProc::Issue  bucketsize %ld \n", bucket->size());
      // }
      //
      // printf("pProcessor::Issue Inst is %ld \n", dinst->getID());

      dinst->setGProc(this);

      StallCause c = add_inst(dinst);
      if (c != NoStall) {
        if (i < RealisticWidth) {
          nStall[c]->add(RealisticWidth - i, dinst->has_stats());
        }
        return i;
      }
      i++;

      bucket->pop();

    } while (!bucket->empty());

    pipeQ.pipeLine.doneItem(bucket);
    pipeQ.instQueue.pop();
  } while (!pipeQ.instQueue.empty());

  return i;
}

bool GProcessor::decode_stage() {
  if (!ROB.empty()) {
    use_stats = ROB.top()->has_stats();
  }

  bool new_clock = adjust_clock(use_stats);
  if (!new_clock) {
    return true;
  }

  /*if(!pipeQ.pipeLine.transient_buffer_empty()) {
    if (spaceInInstQueue >= FetchWidth) {
      IBucket *bucket = pipeQ.pipeLine.next_item_transient_adding_to_rob();
      if (bucket) {
        I(!bucket->empty());
        spaceInInstQueue -= bucket->size();
        pipeQ.instQueue.push(bucket);
    }else {
      noFetch2.inc(use_stats);
    }
 } else {
    noFetch.inc(use_stats);
  }
  }*/
  // ID Stage (insert to instQueue)
  if (spaceInInstQueue >= FetchWidth) {
    IBucket *bucket = pipeQ.pipeLine.nextItem();
    if (bucket) {
      I(!bucket->empty());
      spaceInInstQueue -= bucket->size();
      pipeQ.instQueue.push(bucket);

    } else {
      noFetch2.inc(use_stats);
    }
  } else {
    noFetch.inc(use_stats);
  }

  // IBucket *bucket = pipeQ.instQueue.top();
  // pipeQ.instQueue.push(bucket);
  return false;
}
