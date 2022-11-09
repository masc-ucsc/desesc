// See LICENSE for details.

#include <sys/time.h>
#include <unistd.h>

#include "GProcessor.h"
#include "FetchEngine.h"
#include "GMemorySystem.h"

#include "report.hpp"

Time_t      GProcessor::lastWallClock = 0;

GProcessor::GProcessor(GMemorySystem *gm, CPU_t i)
    : cpu_id(i)
    , FetchWidth(SescConf->getInt("cpusimu", "fetchWidth", i))
    , IssueWidth(SescConf->getInt("cpusimu", "issueWidth", i))
    , RetireWidth(SescConf->getInt("cpusimu", "retireWidth", i))
    , RealisticWidth(RetireWidth < IssueWidth ? RetireWidth : IssueWidth)
    , InstQueueSize(SescConf->getInt("cpusimu", "instQueueSize", i))
    , MaxROBSize(SescConf->getInt("cpusimu", "robSize", i))
    , memorySystem(gm)
    , storeset(i)
    , prefetcher(gm->getDL1(), i)
    , rROB(SescConf->getInt("cpusimu", "robSize", i))
    , ROB(MaxROBSize)
    , rrobUsed("P(%d)_rrobUsed", i)  // avg
    , robUsed("P(%d)_robUsed", i)    // avg
    , nReplayInst("P(%d)_nReplayInst", i)
    , nCommitted("P(%d):nCommitted", i)  // Should be the same as robUsed - replayed
    , noFetch("P(%d):noFetch", i)
    , noFetch2("P(%d):noFetch2", i)
    , nFreeze("P(%d):nFreeze", i)
    , clockTicks("P(%d):clockTicks", i) {
  if (i == 0)
    active = true;
  else
    active = false;

  smt = 1;
  if (SescConf->checkInt("cpusimu", "smt", (int)i))
    smt = SescConf->getInt("cpusimu", "smt", (int)i);
  smt_ctx = i - (i % smt);

  // maxFlows are REAL THreads IDs inside core (GPU SMT)
  if (SescConf->checkInt("cpusimu", "smt")) {
    maxFlows = SescConf->getInt("cpusimu", "smt");
    SescConf->isBetween("cpusimu", "smt", 1, 32);
  } else {
    maxFlows = 1;
  }

  lastReplay = 0;

  activeclock_start = lastWallClock;
  activeclock_end   = lastWallClock;

  throttlingRatio = SescConf->getDouble("cpusimu", "throttlingRatio", i);
  throttling_cntr = 0;
  bool scooremem  = false;
  if (SescConf->checkBool("cpusimu", "scooreMemory", gm->getCoreId()))
    scooremem = SescConf->getBool("cpusimu", "scooreMemory", gm->getCoreId());

  if (scooremem) {
    if ((!SescConf->checkCharPtr("cpusimu", "SL0", i)) && (!SescConf->checkCharPtr("cpusimu", "VPC", i))) {
      printf("ERROR: scooreMemory requested but no SL0 or VPC specified\n");
      fflush(stdout);
      exit(15);
    }
    if ((SescConf->checkCharPtr("cpusimu", "SL0", i)) && (SescConf->checkCharPtr("cpusimu", "VPC", i))) {
      printf("ERROR: scooreMemory requested, cannot have BOTH SL0 and VPC specified\n");
      fflush(stdout);
      exit(15);
    }
  }

  SescConf->isInt("cpusimu", "issueWidth", i);
  SescConf->isLT("cpusimu", "issueWidth", 1025, i);

  SescConf->isInt("cpusimu", "retireWidth", i);
  SescConf->isBetween("cpusimu", "retireWidth", 0, 32700, i);

  SescConf->isInt("cpusimu", "robSize", i);
  SescConf->isBetween("cpusimu", "robSize", 2, 262144, i);

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

  eint = 0;

  buildInstStats("ExeEngine");

#ifdef WAVESNAP_EN
  snap = std::make_unique<Wavesnap>();
#endif

  auto scb_size = Config::get_integer("soc", "core", i , "scb_size",1,2048);
  scb         = std::make_unique<SCB>(scb_size);
}

GProcessor::~GProcessor() {}

void GProcessor::buildInstStats(const std::string &txt) {

  for (int32_t t = 0; t < iMAX; t++) {
    nInst[t] = std::make_unique<Stats_cntr>(fmt::format("P({})_{}_{}:n", cpu_id, txt, Instruction::opcode2Name(static_cast<Opcode>(t))));
  }
}

int32_t GProcessor::issue(PipeQueue &pipeQ) {
  int32_t i = 0;  // Instructions executed counter

  I(!pipeQ.instQueue.empty());

  do {
    IBucket *bucket = pipeQ.instQueue.top();
    // MSG("@%lld  CPU[%d]: Trying to issue instructions from bucket[%p]",(long long int)globalClock,cpu_id,bucket);
    do {
      I(!bucket->empty());
      if (i >= IssueWidth) {
        return i;
      }

      I(!bucket->empty());

      Dinst *dinst = bucket->top();
      //  GMSG(getCoreId()==1,"push to pipe %p", bucket);

      // MSG("@%lld issue dinstID=%lld",globalClock, dinst->getID());

      dinst->setGProc(this);
      StallCause c = addInst(dinst);
      if (c != NoStall) {
        // MSG("@%lld CPU[%d]: stalling dinstID=%lld for %d cycles, reason= %d
        // PE[%d]",globalClock,cpu_id,dinst->getID(),(RealisticWidth-i),c,dinst->getPE());
        if (i < RealisticWidth)
          nStall[c]->add(RealisticWidth - i, dinst->getStatsFlag());
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

void GProcessor::retire() {}
