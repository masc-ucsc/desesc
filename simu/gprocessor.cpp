// See LICENSE for details.

#include "gprocessor.hpp"

#include <sys/time.h>
#include <unistd.h>

#include "fetchengine.hpp"
#include "fmt/format.h"
#include "gmemory_system.hpp"
#include "report.hpp"

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
  //  if (ifid->isBlocked()) {
    // fmt::print("fetch on {}\n", ifid->getMissDinst()->getID());
    //return;
 // }

  auto     smt_hid = hid;  // FIXME: do SMT fetch
  IBucket *bucket  = pipeQ.pipeLine.newItem();
  if(ifid->isBlocked()) {
      // printf("gprocessor::fetch on branchmiss{}%lx\n", ifid->getMissDinst()->getAddr());
       int i=0;
       //auto *transient_dinst = ifid->get_next_transient_dinst() ;
       Addr_t pc = ifid->get_miss_dinst()->getAddr() + 4;

       printf("gprocessor::fetch on branchmiss{}%lx\n",ifid->get_miss_dinst()->getAddr());
       printf("gprocessor::fetch on branchmisspc}%lx\n",ifid->get_miss_dinst()->getAddr()+4);
       printf("gprocessor::fetch on branchmissTransient{}%lx\n",ifid->get_next_transient_dinst()->getAddr() );
       //printf("gProcessor::Yahoo!!!Blocked Inst  %lx ", transient_dinst->getAddr());
       //std::cout<< "gProcessor::Yahoo!!!Blocked Inst Opcode"<<  transient_dinst->getInst()->getOpcodeName()<<std::endl;
       while (i<1){
         //printf("gProcessor:: Entering transient Inst\n");
         auto  *alu_dinst = Dinst::create(Instruction(Opcode::iRALU, RegType::LREG_R3, RegType::LREG_R3, RegType::LREG_R3, RegType::LREG_R3)
                                    ,pc
                                    ,0
                                    ,0
                                    ,true);
         if(alu_dinst)
           printf("gProcessor::Yahoo!!! transient Inst Created %ld and addr is %lx\n", alu_dinst->getID(),alu_dinst->getAddr());
         alu_dinst->setTransient();
         if (bucket) {
           //alu_dinst->setFetchTime();
           bucket->push(alu_dinst);
           //spaceInInstQueue -= bucket->size();
           //pipeQ.instQueue.push(bucket);
           printf("gProcessor::Yahoo!!! Bucket Inst Created %ld and bucket size is %lu", alu_dinst->getID(), bucket->size());
         }
         i++;
       }
       return; 
  }

  if (bucket) {
    ifid->fetch(bucket, eint, smt_hid);
    if (!bucket->empty()) {
      avgFetchWidth.sample(bucket->size(), bucket->top()->has_stats());
      busy = true;
    }
  }
}

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

int32_t GProcessor::issue() {
  int32_t i = 0;  // Instructions executed counter

  printf("\ngProc::Issue Entering issue!!! \n");
  I(!pipeQ.instQueue.empty());

  do {
    IBucket *bucket = pipeQ.instQueue.top();
    do {
      I(!bucket->empty());
      if (i >= IssueWidth) {
        printf("gProc::Issue  Wrong i and  issuewidth is %d %d\n",i, IssueWidth);
        return i;
      }

      I(!bucket->empty());

      Dinst *dinst = bucket->top();
      if(dinst->isTransient())
        printf("gProc::Issue Transient  gets from bucketsize %ld \n",bucket->size());
      else 
        printf("gProc::Issue  bucketsize %ld \n",bucket->size());



      printf("\ngProc::add_inst \n");
      StallCause c = add_inst(dinst);
      if (c != NoStall) {
        if (i < RealisticWidth) {
          nStall[c]->add(RealisticWidth - i, dinst->has_stats());
        }
        return i;
      }
      dinst->setGProc(this);
      i++;

      bucket->pop();

    } while (!bucket->empty());

    pipeQ.pipeLine.doneItem(bucket);
    pipeQ.instQueue.pop();
  } while (!pipeQ.instQueue.empty());

  printf("\ngProc::issue Leaving\n");
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
      printf("\ngProc::decode bucketinstQ id is %lx\n",bucket->top()->getID());
      spaceInInstQueue -= bucket->size();
      pipeQ.instQueue.push(bucket);

    } else {
      noFetch2.inc(use_stats);
    }
  } else {
    noFetch.inc(use_stats);
  }

  printf("\ngProc::decode Leaving\n");
  return false;
}
