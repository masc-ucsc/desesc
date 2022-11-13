// See LICENSE for details.

#include "GProcessor.h"

#include <sys/time.h>
#include <unistd.h>

#include "FetchEngine.h"
#include "GMemorySystem.h"
#include "fmt/format.h"
#include "report.hpp"

GProcessor::GProcessor(GMemorySystem *gm, Hartid_t i)
    : Execute_engine(gm, i)
    , FetchWidth(Config::get_integer("soc", "core", i, "fetch_width"))
    , IssueWidth(Config::get_integer("soc", "core", i, "issue_width"))
    , RetireWidth(Config::get_integer("soc", "core", i, "retire_width"))
    , RealisticWidth(RetireWidth < IssueWidth ? RetireWidth : IssueWidth)
    , InstQueueSize(Config::get_integer("soc", "core", i, "instq_size"))
    , MaxROBSize(Config::get_integer("soc", "core", i, "rob_size", 4))
    , memorySystem(gm)
    , rROB(Config::get_integer("soc", "core", i, "rob_size"))
    , ROB(MaxROBSize)
    , rrobUsed(fmt::format("({})_rrobUsed", i))  // avg
    , robUsed(fmt::format("({})_robUsed", i))    // avg
    , nReplayInst(fmt::format("({})_nReplayInst", i))
    , nCommitted(fmt::format("({}):nCommitted", i))  // Should be the same as robUsed - replayed
    , noFetch(fmt::format("({}):noFetch", i))
    , noFetch2(fmt::format("({}):noFetch2", i)) {
  smt     = Config::get_integer("soc", "core", i, "smt", 1, 32);
  smt_ctx = i - (i % smt);

  maxFlows = smt;

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

  scb        = std::make_shared<Store_buffer>(i);
  storeset   = std::make_shared<StoreSet>(i);
  prefetcher = std::make_shared<Prefetcher>(gm->getDL1(), i);
}

GProcessor::~GProcessor() {}

void GProcessor::buildInstStats(const std::string &txt) {
  for (int32_t t = 0; t < iMAX; t++) {
    nInst[t]
        = std::make_unique<Stats_cntr>(fmt::format("P({})_{}_{}:n", hid, txt, Instruction::opcode2Name(static_cast<Opcode>(t))));
  }
}

int32_t GProcessor::issue(PipeQueue &pipeQ) {
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

      dinst->setGProc(this);
      StallCause c = add_inst(dinst);
      if (c != NoStall) {
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
