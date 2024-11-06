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
    I(globalClock);
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
  printf("FetchEngine::::Entering real fetch !!!\n");
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

//<<<<<<< HEAD
#ifdef ESESC_TRACE_DATA
    bool predictable = false;

#if 0
    if (dinst->getInst()->isControl()) {
      bool p=bpred->get_Miss_Pred_Bool_Val();

      if(p) { //p=1 Miss Prediction
        uint64_t pc_br = dinst->getPC();
        pc_br = pc_br<<16;

        nbranchMissHist.sample(dinst->has_stats(),pc_br);

        uint64_t ldpc_Src1 = oracleDataRAT[dinst->getInst()->getSrc1()].ldpc;
        uint64_t ldpc_Src2 = oracleDataRAT[dinst->getInst()->getSrc2()].ldpc;
        uint64_t load_data_Src1 = oracleDataLast[ldpc_Src1].addr;
        uint64_t load_addr_Src1 = oracleDataLast[ldpc_Src1].data;

        load_data_Src1=pc_br|(load_data_Src1 & 0xFFFF);
        load_addr_Src1=pc_br|(load_addr_Src1 & 0xFFFF);

        uint64_t load_data_Src2 = oracleDataLast[ldpc_Src2].addr;
        uint64_t load_addr_Src2 = oracleDataLast[ldpc_Src2].data;

        load_data_Src2=pc_br|(load_data_Src2 & 0xFFFF);
        load_addr_Src2=pc_br|(load_addr_Src2 & 0xFFFF);
        nLoadData_per_branch.sample(dinst->has_stats(),load_data_Src1);//pc|16bit data cnt++
        nLoadData_per_branch.sample(dinst->has_stats(),load_data_Src2);//cnt++ same data stucture
        nLoadAddr_per_branch.sample(dinst->has_stats(),load_addr_Src1);//pc|16bit data cnt++
        nLoadAddr_per_branch.sample(dinst->has_stats(),load_addr_Src2);//cnt++ same data stucture
      }//p ends
    }//control
#endif

#if 0
    //update load data buffer if there is any older store for the same address
    if(dinst->getInst()->getOpcode() == iSALU_ST) {
      Addr_t st_addr = dinst->getAddr();
      for(int i = 0; i < DL1->getLdBuffSize(); i++) {
        Addr_t saddr = DL1->load_data_buffer[i].start_addr;
        Addr_t eaddr = DL1->load_data_buffer[i].end_addr;
        int64_t del   = DL1->load_data_buffer[i].delta;
        if(st_addr >= saddr && st_addr <= eaddr) {
          int idx        = 0;
          if(del != 0) {
            idx = abs((int)(st_addr - saddr) / del);
          }
          if(st_addr == (idx * del + saddr)) { //check if st_addr exactly matches this entry in buff
            DL1->load_data_buffer[i].req_data[idx] = dinst->getData2();
            DL1->load_data_buffer[i].valid[idx] = true;
            DL1->load_data_buffer[i].marked[idx] = true;
          }
        }
        Addr_t saddr2 = DL1->load_data_buffer[i].start_addr2;
        Addr_t eaddr2 = DL1->load_data_buffer[i].end_addr2;
        int64_t del2   = DL1->load_data_buffer[i].delta2;
        if(st_addr >= saddr2 && st_addr <= eaddr2) {
          int idx2        = 0;
          if(del2 != 0) {
            idx2 = abs((int)(st_addr - saddr2) / del2);
          }
          if(st_addr == (idx2 * del2 + saddr2)) { //check if st_addr exactly matches this entry in buff
            DL1->load_data_buffer[i].req_data2[idx2] = dinst->getData2();
            DL1->load_data_buffer[i].valid2[idx2] = true;
            DL1->load_data_buffer[i].marked2[idx2] = true;
          }
        }
      }
    }
#endif

    if (dinst->getInst()->isLoad()) {
      bool ld_tracking = false;

#if 0
      //insert load into load data buff table if load's address is a hit on table
      Addr_t ld_addr = dinst->getAddr();
      for(int i = 0; i < DL1->getLdBuffSize(); i++) {
        Addr_t saddr = DL1->load_data_buffer[i].start_addr;
        Addr_t eaddr = DL1->load_data_buffer[i].end_addr;
        int64_t del   = DL1->load_data_buffer[i].delta;
        if(ld_addr >= saddr && ld_addr <= eaddr) {
          int idx = 0;
          if(del != 0) {
            idx = abs(((int)(ld_addr - saddr) / del));
          }
          if(ld_addr == DL1->load_data_buffer[i].req_addr[idx] && !DL1->load_data_buffer[i].marked[idx]) { //check if st_addr exactly matches this entry in buff
            DL1->load_data_buffer[i].req_data[idx] = dinst->getData();
            DL1->load_data_buffer[i].valid[idx] = true;
          }
        }
        Addr_t saddr2 = DL1->load_data_buffer[i].start_addr2;
        Addr_t eaddr2 = DL1->load_data_buffer[i].end_addr2;
        int64_t del2   = DL1->load_data_buffer[i].delta2;
        if(ld_addr >= saddr2 && ld_addr <= eaddr2) {
          int idx2 = 0;
          if(del2 != 0) {
            idx2 = abs(((int)(ld_addr - saddr2) / del2));
          }
          if(ld_addr == DL1->load_data_buffer[i].req_addr2[idx2] && !DL1->load_data_buffer[i].marked2[idx2]) { //check if st_addr exactly matches this entry in buff
            DL1->load_data_buffer[i].req_data2[idx2] = dinst->getData();
            DL1->load_data_buffer[i].valid2[idx2] = true;
          }
        }
      }
#endif

      // ld_tracking = false;

      if (ideal_apred) {
#if 0
        predictable = true; // FIXME2: ENABLE MAGIC/ORACLE PREDICTION FOR ALL
#else
        auto   val           = ideal_apred->exe_update(dinst->getPC(), dinst->getAddr(), dinst->getData());
        int    prefetch_conf = ideal_apred->ret_update(dinst->getPC(), dinst->getAddr(), dinst->getData());
        Addr_t naddr         = ideal_apred->predict(dinst->getPC(), 0, false);  // ideal, predict after update

        if (naddr == dinst->getAddr()) {
          predictable = true;
        }
        // FIXME can we mark LD as prefetchable here if predictable == true????

        if (ld_tracking) {
          printf("predictable:%d ldpc:%llx naddr:%llx addr:%llx %d data:%d\n",
                 predictable ? 1 : 0,
                 dinst->getPC(),
                 naddr,
                 dinst->getAddr(),
                 dinst->getInst()->getDst1(),
                 dinst->getData());
        }
#endif
      }

      if (predictable) {
        printf("FetchEngine::OracleDataRAT Fetched Inst is %ld \n", dinst->getID());
        oracleDataRAT[dinst->getInst()->getDst1()].depth = 0;
        oracleDataRAT[dinst->getInst()->getDst1()].ldpc  = dinst->getPC();
        lastPredictable_ldpc                             = dinst->getPC();
        lastPredictable_addr                             = dinst->getAddr();
        lastPredictable_data                             = dinst->getData();
        oracleDataLast[dinst->getPC()].set(dinst->getData(), dinst->getAddr());
      } else {
        printf("FetchEngine::OracleDataRAT Fetched Inst is %ld \n", dinst->getID());
        oracleDataRAT[dinst->getInst()->getDst1()].depth = 32;
        oracleDataRAT[dinst->getInst()->getDst1()].ldpc  = 0;
        oracleDataLast[dinst->getPC()].clear(dinst->getData(), dinst->getAddr());
      }

      if (oracleDataLast[dinst->getPC()].isChained()) {
        printf("FetchEngine::OracleDataRAT Fetched Inst is %ld \n", dinst->getID());
        dinst->setChain(this, oracleDataLast[dinst->getPC()].inc_chain());
      }
    }

#if 1
    if (!dinst->getInst()->isLoad() && dinst->getInst()->isBranch()) {  // Not for LD-LD chain
      // this loop tracks LD-BR dependency for now
      //  Copy Other
      printf("Fetchengine::realfetch oracleDATARAT branch() instID %ld\n", dinst->getID());
      int    d = 32768;
      Addr_t ldpc;
      int    d1          = oracleDataRAT[dinst->getInst()->getSrc1()].depth;
      int    d2          = oracleDataRAT[dinst->getInst()->getSrc2()].depth;
      Addr_t ldpc2       = 0;
      int    dep_reg_id1 = -1;
      int    dep_reg_id2 = -1;

      if (d1 < d2 && d1 < 3) {
        d           = d1;
        ldpc        = oracleDataRAT[dinst->getInst()->getSrc1()].ldpc;
        dep_reg_id1 = dinst->getInst()->getSrc1();
#if 1
        if (d2 < 4) {
          ldpc2       = oracleDataRAT[dinst->getInst()->getSrc2()].ldpc;
          dep_reg_id2 = dinst->getInst()->getSrc2();
        }
#endif
      } else if (d2 < d1 && d2 < 3) {
        d           = d2;
        ldpc        = oracleDataRAT[dinst->getInst()->getSrc2()].ldpc;
        dep_reg_id2 = dinst->getInst()->getSrc2();
#if 1
        if (d1 < 4) {
          ldpc2       = oracleDataRAT[dinst->getInst()->getSrc1()].ldpc;
          dep_reg_id1 = dinst->getInst()->getSrc1();
        }
#endif
      } else if (d1 == d2 && d1 < 3) {
        // Closest ldpc
        Addr_t x1 = dinst->getPC() - oracleDataRAT[dinst->getInst()->getSrc1()].ldpc;
        Addr_t x2 = dinst->getPC() - oracleDataRAT[dinst->getInst()->getSrc2()].ldpc;
        if (d1 < d2) {
          d = d1;
        } else {
          d = d2;
        }
        if (x1 < x2) {
          ldpc        = oracleDataRAT[dinst->getInst()->getSrc1()].ldpc;
          dep_reg_id1 = dinst->getInst()->getSrc1();
          if (d2 < 2) {
            ldpc2       = oracleDataRAT[dinst->getInst()->getSrc2()].ldpc;
            dep_reg_id2 = dinst->getInst()->getSrc2();
          }
        } else {
          ldpc        = oracleDataRAT[dinst->getInst()->getSrc2()].ldpc;
          dep_reg_id2 = dinst->getInst()->getSrc2();
          if (d1 < 2) {
            ldpc2       = oracleDataRAT[dinst->getInst()->getSrc1()].ldpc;
            dep_reg_id1 = dinst->getInst()->getSrc1();
          }
        }
      } else {
        d    = 32768;
        ldpc = 0;
      }

#ifdef SBPT_JUSTDELTA0
      if (ldpc && oracleDataLast[ldpc].delta0) {
        d    = 32768;
        ldpc = 0;
      }
      if (ldpc2 && oracleDataLast[ldpc2].delta0) {
        ldpc2 = 0;
      }
#endif

      if (ldpc) {
        oracleDataRAT[dinst->getInst()->getDst1()].depth = d + 1;
        oracleDataRAT[dinst->getInst()->getDst1()].ldpc  = ldpc;

        Addr_t data = oracleDataLast[ldpc].data;
        Addr_t addr = oracleDataLast[ldpc].addr;

        // bool tracking = dinst->getPC() == 0x12001b870 && dinst->has_stats();
        // bool tracking = dinst->getPC() == 0x100072dc && dinst->has_stats();
        bool tracking = dinst->getPC() == 0x10006548;
        tracking      = false;

        if (dinst->getInst()->isBranch()) {
          ldpc2brpc[ldpc] = dinst->getPC();  // Not used now. Once prediction is updated

          // I(dinst->getDataSign() == DS_NoData);

#ifdef SBPT_JUSTLAST
          data  = lastPredictable_data;
          ldpc  = lastPredictable_ldpc;
          ldpc2 = 0;
          d     = 3;
#else
#endif
          Addr_t x = dinst->getPC() - ldpc;
          if (d < 4) {
            int dep_reg = -1;
            dinst->setDataSign(data, ldpc);
            dinst->setLdAddr(addr);  // addr or lastPredictable_addr???

#if 1
            if (ldpc2) {
#if 1
              // Always chain branches when both can be tracked
              oracleDataLast[ldpc].chain();
              oracleDataLast[ldpc2].chain();
#endif
              Addr_t data3 = oracleDataLast[ldpc2].data;
              dinst->addDataSign(d, data3, ldpc2);  // Trigger direct compare
              if (tracking) {
                printf("xxbr br ldpc:%llx %s ds:%d data:%d data3:%d ldpc:%llx ldpc2:%llx d:%d\n",
                       dinst->getPC(),
                       dinst->isTaken() ? "T" : "NT",
                       dinst->getDataSign(),
                       data,
                       data3,
                       ldpc,
                       ldpc2,
                       d);
              }
            } else {
              if (tracking) {
                printf("yybr br ldpc:%llx %s ds:%d data:%d ldpc:%llx ldpc2:%llx d:%d\n",
                       dinst->getPC(),
                       dinst->isTaken() ? "T" : "NT",
                       dinst->getDataSign(),
                       data,
                       ldpc,
                       ldpc2,
                       d);
              }
            }
#endif
          } else {
            if (tracking) {
              ldpc = lastPredictable_ldpc;
              data = lastPredictable_data;
              addr = lastPredictable_addr;
              printf("nopr br ldpc:%llx %s data:%d ldpc:%llx ldaddr:%llx r%d:%d r%d:%d\n",
                     dinst->getPC(),
                     dinst->isTaken() ? "T" : "NT",
                     data,
                     ldpc,
                     addr,
                     dinst->getInst()->getSrc1(),
                     d1,
                     dinst->getInst()->getSrc2(),
                     d2);
            }
          }
        }
      }
    }
#endif

#ifdef ENABLE_LDBP
    if (dinst->getInst()->isBranch()) {
      // NEW INTERFACE  !!!!!!
      printf("FetchEngine::LDBP Fetched Inst is %ld \n", dinst->getID());

      // check if BR PC is present in BOT
      int  bot_idx        = DL1->return_bot_index(dinst->getPC());
      bool all_data_valid = true;
      if (bot_idx != -1) {
        if (DL1->bot_vec[bot_idx].outcome_ptr >= DL1->getLotQueueSize()) {
          DL1->bot_vec[bot_idx].outcome_ptr = 0;
        }
        int q_idx = (DL1->bot_vec[bot_idx].outcome_ptr++) % DL1->getLotQueueSize();
        for (int i = 0; i < DL1->bot_vec[bot_idx].load_ptr.size(); i++) {
          Addr_t ldpc = DL1->bot_vec[bot_idx].load_ptr[i];
          // int lor_idx   = DL1->return_lor_index(ldpc);
          int lor_idx = DL1->compute_lor_index(dinst->getPC(), ldpc);
          int lt_idx  = DL1->return_load_table_index(ldpc);
          if (lor_idx != -1 && (DL1->lor_vec[lor_idx].brpc == dinst->getPC()) && (DL1->lor_vec[lor_idx].ld_pointer == ldpc)) {
            if (1 || DL1->lor_vec[lor_idx].use_slice) {  // when LD-BR slice goes through LD (use_slice == 1)
              dinst->set_trig_ld_status();
              // increment lor.data_pos too
              // DL1->lor_vec[lor_idx].data_pos++;
              // Addr_t curr_addr = DL1->lor_vec[lor_idx].ld_start + q_idx * DL1->lor_vec[lor_idx].ld_delta;
              DL1->bot_vec[bot_idx].curr_br_addr[i] += DL1->lor_vec[lor_idx].ld_delta;
              Addr_t curr_addr = DL1->bot_vec[bot_idx].curr_br_addr[i];
              // DL1->bot_vec[bot_idx].curr_br_addr[i] += DL1->lor_vec[lor_idx].ld_delta;
              // Addr_t curr_addr = DL1->load_table_vec[lt_idx].ld_addr + DL1->load_table_vec[lt_idx].delta;
              int    valid  = DL1->lot_vec[lor_idx].valid[q_idx];
              Addr_t q_addr = DL1->lot_vec[lor_idx].tl_addr[q_idx];
              if (!DL1->lot_vec[lor_idx].valid[q_idx]) {
                all_data_valid = false;
                dinst->inc_trig_ld_status();
              } else {
                DL1->lot_vec[lor_idx].valid[q_idx] = 0;
              }
            } else {
              // when use_slice == 0
#if 1
              int curr_br_outcome = dinst->isTaken();
              if (DL1->bot_vec[bot_idx].br_flip == curr_br_outcome) {
                all_data_valid = false;
                break;
              }
              if (DL1->lot_vec[lor_idx].valid[q_idx] && all_data_valid) {
                int lot_qidx                          = (q_idx + 1) % DL1->getLotQueueSize();
                DL1->lot_vec[lor_idx].valid[lot_qidx] = 1;
                DL1->lot_vec[lor_idx].valid[q_idx]    = 0;
              } else {
                all_data_valid = false;
              }
#endif
            }
          } else {  // if not the correct LD-BR pair; no match in LOR and BOT stride-pointers
            all_data_valid = false;
          }
        }
      } else {  // if Br not in BOT, do not use LDBP
        all_data_valid = false;
      }

      if (all_data_valid) {
        // USE LDBP
        dinst->setUseLevel3();  // enable use_ldbp FLAG
      }
    }
#endif

#endif

//=======
//>>>>>>> upstream/main
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

  /*if(fid->isBlocked()) {
   pipeQ.pipeLine.readyItem(bucket);//must bucket-> markedfetch()
   return;
  }*/
  bpred->fetchBoundaryEnd();

  if (il1_enable && !bucket->empty()) {
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
