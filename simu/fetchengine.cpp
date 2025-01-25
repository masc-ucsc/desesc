// See LICENSE for details.

#include "fetchengine.hpp"

#include <climits>

#include "absl/strings/str_split.h"
#include "addresspredictor.hpp"
#include "alloca.h"
#include "config.hpp"
#include "fmt/format.h"
#include "gmemory_system.hpp"
#include "gprocessor.hpp"
#include "memobj.hpp"
#include "memrequest.hpp"
#include "pipeline.hpp"
#include "taskhandler.hpp"
#include "tracer.hpp"
extern bool MIMDmode;

// #define ENABLE_FAST_WARMUP 1
// #define FETCH_TRACE 1

// SBPT: Do not track RAT, just use last predictable LD
// #define SBPT_JUSTLAST 1
// #define SBPT_JUSTDELTA0 1

FetchEngine::FetchEngine(Hartid_t id, std::shared_ptr<Gmemory_system> gms_, std::shared_ptr<BPredictor> shared_bpred)
    : gms(gms_)
    , avgFetchLost(fmt::format("({})_FetchEngine_avgFetchLost", id))
    , avgBranchTime(fmt::format("({})_FetchEngine_avgBranchTime", id))
    , avgBranchTime2(fmt::format("({})_FetchEngine_avgBranchTime2", id))
    , avgFetchTime(fmt::format("({})_FetchEngine_avgFetchTime", id))
    , avgFetched(fmt::format("({})_FetchEngine_avgFetched", id))
    , nDelayInst1(fmt::format("({})_FetchEngine:nDelayInst1", id))
    , nDelayInst2(fmt::format("({})_FetchEngine:nDelayInst2", id))  // Not enough BB/LVIDs per cycle)
    , nDelayInst3(fmt::format("({})_FetchEngine:nDelayInst3", id))
    , nBTAC(fmt::format("({})_FetchEngine:nBTAC", id))  // BTAC corrections to BTB
    , zeroDinst(fmt::format("({})_zeroDinst:nBTAC", id))
//  ,szBB("FetchEngine(%d):szBB", id)
//  ,szFB("FetchEngine(%d):szFB", id)
//  ,szFS("FetchEngine(%d):szFS", id)
//,unBlockFetchCB(this)
//,unBlockFetchBPredDelayCB(this)
{
  fetch_width = Config::get_power2("soc", "core", id, "fetch_width", 1, 1024);

  half_fetch_width = fetch_width / 2;

  fetch_align = Config::get_bool("soc", "core", id, "fetch_align");
  trace_align = Config::get_bool("soc", "core", id, "trace_align");

  if (Config::has_entry("soc", "core", id, "fetch_one_line")) {
    fetch_one_line = Config::get_bool("soc", "core", id, "fetch_one_line");
  } else {
    fetch_one_line = !trace_align;
  }

  max_bb_cycle = Config::get_integer("soc", "core", id, "max_bb_cycle", 1, 1024);

  if (shared_bpred) {
    bpred = std::make_shared<BPredictor>(id, gms->getIL1(), gms->getDL1(), shared_bpred);
  } else {
    bpred = std::make_shared<BPredictor>(id, gms->getIL1(), gms->getDL1());
  }

  missInst = false;
  // for flushing transient from pipeline
  is_fetch_next_ready = false;
  // Move to libmem/Prefetcher.cpp ; it can be stride or DVTAGE
  // FIXME: use AddressPredictor::create()

  std::vector<std::string> v        = absl::StrSplit(Config::get_string("soc", "core", id, "il1"), ' ');
  auto                     isection = v[0];

  il1_enable = Config::get_bool("soc", "core", id, "caches");

  il1_line_size = Config::get_power2(isection, "line_size", fetch_width * 2, 8192);
  il1_line_bits = log2i(il1_line_size);

  // Get some icache L1 parameters
  il1_hit_delay = Config::get_integer(isection, "delay");

  lastMissTime = 0;
}

FetchEngine::~FetchEngine() {}

bool FetchEngine::processBranch(Dinst *dinst, uint16_t n2Fetch) {
  // printf("FetchEngine::processbranch entering dinstID %ld\n", dinst->getID());

  (void)n2Fetch;
  I(dinst->getInst()->isControl());  // getAddr is target only for br/jmp

  bool        fastfix;
  TimeDelta_t delay = bpred->predict(dinst, &fastfix);

  if (delay == 0) {
    return false;
  }
  // I(dinst->getGProc());
  setMissInst(dinst);
  // is_fetch_next_ready = false;
  setTransientInst(dinst);

  Time_t n = (globalClock - lastMissTime);
  avgFetchTime.sample(n, dinst->has_stats());

#if 0
  if (!dinst->isBiasBranch()) {
    if ( dinst->isTaken() && (dinst->getAddr() > dinst->getPC() && (dinst->getAddr() + 8<<2) <= dinst->getPC())) {
      fastfix = true;
    }
  }
#endif

  if (fastfix) {
    // dinst->getGProc()->flush_transient_inst_on_fetch_ready();
    unBlockFetchBPredDelayCB::schedule(delay, this, dinst, globalClock);
  } else {
    dinst->lockFetch(this);
  }
  // printf("FetchEngine::processbranch return true dinstID %ld\n", dinst->getID());

  return true;
}

void FetchEngine::chainPrefDone(Addr_t pc, int distance, Addr_t addr) {
  (void)pc;
  (void)distance;
  (void)addr;
#if 0
  bool hit = !(DL1->invalid());
    if(hit)
      do ldbp
    else
      trigger new prefetch if it can be timely
#endif
}

void FetchEngine::chainLoadDone(Dinst *dinst) { (void)dinst; }

void FetchEngine::realfetch(IBucket *bucket, std::shared_ptr<Emul_base> eint, Hartid_t fid, int32_t n2Fetch, GProcessor *gproc) {
  // printf("FetchEngine::::Entering real fetch !!!\n");
  Addr_t lastpc = 0;

#ifdef USE_FUSE
  RegType last_dest = LREG_R0;
  RegType last_src1 = LREG_R0;
  RegType last_src2 = LREG_R0;
#endif

  do {
    Dinst *dinst = eint->peek(fid);
    if (dinst == nullptr) {  // end of trace
      TaskHandler::simu_pause(fid);
      break;
    }

#ifdef ENABLE_FAST_WARMUP
    if (dinst->getPC() == 0) {  // FIXME: W mode, counter, not this
      do {
        EventScheduler::advanceClock();
      } while (gms->getDL1()->isBusy(dinst->getAddr()));

      if (dinst->getInst()->isLoad()) {
        MemRequest::sendReqReadWarmup(gms->getDL1(), dinst->getAddr());
        dinst->scrap(eint);
        dinst = 0;
      } else if (dinst->getInst()->isStore()) {
        MemRequest::sendReqWriteWarmup(gms->getDL1(), dinst->getAddr());
        dinst->scrap(eint);
        dinst = 0;
      } else {
        I(0);
        dinst->scrap(eint);
        dinst = 0;
      }
      continue;
    }
#endif
    if (lastpc == 0) {
      bpred->fetchBoundaryBegin(dinst);
      if (!trace_align) {
        uint64_t entryPC = dinst->getPC() >> 2;

        uint16_t fetchLost;

        if (fetch_align) {
          fetchLost = (entryPC) & (fetch_width - 1);
        } else {
          fetchLost = (entryPC) & (half_fetch_width - 1);
        }

        // No matter what, do not pass cache line boundary
        uint16_t fetchMaxPos = (entryPC & (il1_line_size / 4 - 1)) + fetch_width;
        if (fetchMaxPos > (il1_line_size / 4)) {
          fetchLost += (fetchMaxPos - il1_line_size / 4);
        }

        avgFetchLost.sample(fetchLost, dinst->has_stats());

        n2Fetch -= fetchLost;
      }

      n2Fetch--;
    } else {
      I(lastpc);

      if ((lastpc + 4) == dinst->getPC() || (lastpc + 2) == dinst->getPC()) {
      } else {
        maxBB--;
        if (maxBB < 1) {
          nDelayInst2.add(n2Fetch, dinst->has_stats());
          dinst->scrap();
          break;
        }
      }
      n2Fetch--;

      if (fetch_one_line) {
        if ((lastpc >> il1_line_bits) != (dinst->getPC() >> il1_line_bits)) {
          dinst->scrap();
          break;
        }
      }
    }
    lastpc = dinst->getPC();

    eint->execute(fid);

    dinst->setGProc(gproc);
    // dinst->dump("TR");
    Tracer::stage(dinst, "IF");
    //<<<<<<< HEAD
    // printf("FetchEngine::::Fetched Inst is %ld at clock cycle %ld \n", dinst->getID(), globalClock);
    // std::cout << "FetchEngine:::Fetched Inst Opcode is " << dinst->getInst()->getOpcodeName() << "and asm is "
    //           << dinst->getInst()->get_asm() << std::endl;
    dinst->setFetchTime();
    bucket->push(dinst);

#ifdef USE_FUSE
    if (dinst->getInst()->isControl()) {
      RegType src1 = dinst->getInst()->getSrc1();
      if (dinst->getInst()->doesJump2Label() && dinst->getInst()->getSrc2() == LREG_R0
          && (src1 == last_dest || src1 == last_src1 || src1 == last_src2 || src1 == LREG_R0)) {
        dinst->scrap(eint);
        continue;
      }
    }
#endif

#ifdef FETCH_TRACE
    static int bias_ninst   = 0;
    static int bias_firstPC = 0;
    bias_ninst++;
#endif
    // I(dinst->getGProc());

    if (dinst->getInst()->isControl()) {
      // printf("FetchEngine::realfetch instID before processbranch %ld\n", dinst->getID());
      // I(dinst->getGProc());
      bool stall_fetch = processBranch(dinst, n2Fetch);
      if (stall_fetch) {
#ifdef FETCH_TRACE
        if (dinst->isBiasBranch() && dinst->getFetchEngine()) {
          // OOPS. Thought that it was bias and it is a long misspredict
          bias_firstPC = dinst->getAddr();
          bias_ninst   = 0;
        }
        if (bias_ninst > 256) {
          bias_firstPC = dinst->getAddr();
          bias_ninst   = 0;
        }
#endif
        break;
      }
#ifdef FETCH_TRACE
      if (bias_ninst > 256) {
        bias_firstPC = dinst->getAddr();
        bias_ninst   = 0;
      } else if (!dinst->isBiasBranch()) {
        if (dinst->isTaken() && (dinst->getAddr() > dinst->getPC() && (dinst->getAddr() + 8 << 2) <= dinst->getPC())) {
          // Move instructions to predicated (up to 8)
        } else {
          bias_firstPC = dinst->getAddr();
          bias_ninst   = 0;
        }
      }
#endif
    }

#ifdef USE_FUSE
    last_dest = dinst->getInst()->getDst1();
    last_src1 = dinst->getInst()->getSrc1();
    last_src2 = dinst->getInst()->getSrc2();
#endif

    // Fetch uses getHead, ROB retires getTail
  } while (n2Fetch > 0);

  bpred->fetchBoundaryEnd();

  if (false && il1_enable && !bucket->empty()) {
    avgFetched.sample(bucket->size(), bucket->top()->has_stats());
    MemRequest::sendReqRead(gms->getIL1(),
                            bucket->top()->has_stats(),
                            bucket->top()->getPC(),
                            0xdeaddead,
                            &(bucket->markFetchedCB));  // 0xdeaddead as PC signature
  } else {
    bucket->markFetchedCB.schedule(il1_hit_delay);
  }
}

void FetchEngine::fetch(IBucket *bucket, std::shared_ptr<Emul_base> eint, Hartid_t fid, GProcessor *gproc) {
  // Reset the max number of BB to fetch in this cycle (decreased in processBranch)
  maxBB = max_bb_cycle;

  // You pass maxBB because there may be many fetches calls to realfetch in one cycle
  // (thanks to the callbacks)
  realfetch(bucket, eint, fid, fetch_width, gproc);
}

void FetchEngine::dump(const std::string &str) const { bpred->dump(str + "_FE"); }

void FetchEngine::unBlockFetchBPredDelay(Dinst *dinst, Time_t missFetchTime) {
  // printf("FetchEngine::unBlockFetchBpreddelay entering dinstID %ld at clock cycle %ld\n", dinst->getID(), globalClock);
  // dinst->getGProc()->flush_transient_inst_on_fetch_ready();
  clearMissInst(dinst, missFetchTime);
  dinst->getGProc()->flush_transient_inst_on_fetch_ready();
  is_fetch_next_ready = true;

  Time_t n = (globalClock - missFetchTime);
  avgBranchTime2.sample(n, dinst->has_stats());  // Not short branches
  // n *= fetch_width; // FOR CPU
  n *= 1;  // FOR GPU

  nDelayInst3.add(n, dinst->has_stats());
}

void FetchEngine::unBlockFetch(Dinst *dinst, Time_t missFetchTime) {
  // printf("FetchEngine::unBlockFetch  entering dinstID %ld\n", dinst->getID());
  clearMissInst(dinst, missFetchTime);
  // is_fetch_next_ready = true;
  dinst->getGProc()->flush_transient_inst_on_fetch_ready();
  I(missFetchTime != 0 || globalClock < 1000);  // The first branch can have time zero fetch

  I(globalClock > missFetchTime);
  Time_t n = (globalClock - missFetchTime);
  avgBranchTime.sample(n, dinst->has_stats());  // Not short branches
  // n *= fetch_width;  //FOR CPU
  n *= 1;  // FOR GPU and for MIMD
  nDelayInst1.add(n, dinst->has_stats());

  lastMissTime = globalClock;
}

void FetchEngine::clearMissInst(Dinst *dinst, Time_t missFetchTime) {
  (void)dinst;
  (void)missFetchTime;

  I(missInst);
  missInst = false;

#ifndef NDEBUG
  I(dinst == missDinst);
  missDinst = 0;
#endif

  cbPending.mycall();
}

void FetchEngine::setMissInst(Dinst *dinst) {
  (void)dinst;
  // if(!dinst->isTransient())
  I(!missInst);

  missInst = true;
#ifndef NDEBUG
  missDinst = dinst;
#endif
}

void FetchEngine::setTransientInst(Dinst *dinst) {
  (void)dinst;
  transientDinst = dinst;
}
