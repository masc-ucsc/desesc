// See LICENSE for details.

#include "bpred.hpp"

#include <alloca.h>
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

#include <fstream>
#include <ios>
#include <iostream>

#include "config.hpp"
#include "fmt/format.h"
#include "imlibest.hpp"
#include "memobj.hpp"
#include "report.hpp"

// #define CLOSE_TARGET_OPTIMIZATION 1

/*****************************************
 * BPred
 */

BPred::BPred(int32_t i, const std::string &sec, const std::string &sname, const std::string &name)
    : id(i)
    , nHit(fmt::format("P({})_BPred{}_{}:nHit", i, sname, name))
    , nMiss(fmt::format("P({})_BPred{}_{}:nMiss", i, sname, name)) {
  addrShift = Config::get_integer(sec, "bp_addr_shift");

  maxCores = Config::get_array_size("soc", "core");
}

BPred::~BPred() {}

void BPred::fetchBoundaryBegin(Dinst *dinst) {
  (void)dinst;
  // No fetch boundary implemented (must be specialized per predictor if supported)
}

void BPred::fetchBoundaryEnd() {}

/*****************************************
 * RAS
 */
BPRas::BPRas(int32_t i, const std::string &section, const std::string &sname)
    : BPred(i, section, sname, "RAS")
    , RasSize(Config::get_integer(section, "ras_size", 0, 128))
    , rasPrefetch(Config::get_bool(section, "ras_prefetch")) {
  if (RasSize == 0) {
    return;
  }

  stack.resize(RasSize);
  index = 0;
}

BPRas::~BPRas() {}

void BPRas::tryPrefetch(MemObj *il1, bool doStats, int degree) {
  if (rasPrefetch == 0) {
    return;
  }

  for (int j = 0; j < rasPrefetch; j++) {
    int i = index - j - 1;
    while (i < 0) {
      i = RasSize - 1;
    }

    il1->tryPrefetch(stack[i], doStats, degree, PSIGN_RAS, PSIGN_RAS);
  }
}

Outcome BPRas::predict(Dinst *dinst, bool doUpdate, bool doStats) {
  (void)doStats;
  // RAS is a little bit different than other predictors because it can update
  // the state without knowing the oracleNextPC. All the other predictors update the
  // statistics when the branch is resolved. RAS automatically updates the
  // tables when predict is called. The update only actualizes the statistics.

  if (dinst->getInst()->isFuncRet()) {
    if (RasSize == 0) {
      return Outcome::Correct;
    }

    if (doUpdate) {
      index--;
      if (index < 0) {
        index = RasSize - 1;
      }
    }

    uint64_t phase = 0;
    for (int i = 0; i < 4; i++) {
      int pos = index - i;
      if (pos < 0) {
        pos = RasSize - 1;
      }
      phase = (phase >> 3) ^ stack[index];
    }

#ifdef USE_DOLC
    // idolc.setPhase(phase);
#endif

    if (stack[index] == dinst->getAddr() || (stack[index] + 4) == dinst->getAddr() || (stack[index] + 2) == dinst->getAddr()) {
      return Outcome::Correct;
    }

    return Outcome::Miss;
  } else if (dinst->getInst()->isFuncCall() && RasSize) {
    if (doUpdate) {
      stack[index] = dinst->getPC();
      index++;

      if (index >= RasSize) {
        index = 0;
      }
    }
  }

  return Outcome::None;
}

/*****************************************
 * BTB
 */
BPBTB::BPBTB(int32_t i, const std::string &section, const std::string &sname, const std::string &name)
    : BPred(i, section, sname, name), nHitLabel(fmt::format("P({})_BPred{}_{}:nHitLabel", i, sname, name)) {
  btbHistorySize = Config::get_integer(section, "btb_history_size");

  if (btbHistorySize) {
    dolc = new DOLC(btbHistorySize + 1, 5, 2, 2);
  } else {
    dolc = 0;
  }

  btbicache = Config::get_bool(section, "btb_split_il1");

  if (Config::get_integer(section, "btb_size") == 0) {
    // Oracle
    data = 0;
    return;
  }

  data = BTBCache::create(section, "btb", fmt::format("P({})_BPred{}_BTB:", i, sname));
  I(data);
}

BPBTB::~BPBTB() {
  if (data) {
    data->destroy();
  }
}

void BPBTB::updateOnly(Dinst *dinst) {
  if (data == 0 || !dinst->isTaken()) {
    return;
  }

  uint32_t key = calcHist(dinst->getPC());

  BTBCache::CacheLine *cl = data->fillLine(key, 0xdeaddead);
  I(cl);

  cl->inst = dinst->getAddr();
}

Outcome BPBTB::predict(Dinst *dinst, bool doUpdate, bool doStats) {
  bool ntaken = !dinst->isTaken();

  if (data == 0) {
    // required when BPOracle
    if (dinst->getInst()->doesCtrl2Label()) {
      nHitLabel.inc(doUpdate && dinst->has_stats() && doStats);
    } else {
      nHit.inc(doUpdate && dinst->has_stats() && doStats);
    }

    if (ntaken) {
      // Trash result because it shouldn't have reach BTB. Otherwise, the
      // worse the predictor, the better the results.
      return Outcome::NoBTB;
    }
    return Outcome::Correct;
  }

  uint32_t key = calcHist(dinst->getPC());

  if (dolc) {
    if (doUpdate) {
      dolc->update(dinst->getPC());
    }

    key ^= dolc->getSign(btbHistorySize, btbHistorySize);
  }

  if (ntaken || !doUpdate) {
    // The branch is not taken. Do not update the cache
    if (dinst->getInst()->doesCtrl2Label() && btbicache) {
      nHitLabel.inc(doStats && doUpdate && dinst->has_stats());
      return Outcome::NoBTB;
    }

    BTBCache::CacheLine *cl = data->readLine(key);

    if (cl == 0) {
      nHit.inc(doStats && doUpdate && dinst->has_stats());
      return Outcome::NoBTB;  // Outcome::NoBTB because BTAC would hide the prediction
    }

    if (cl->inst == dinst->getAddr()) {
      nHit.inc(doStats && doUpdate && dinst->has_stats());
      return Outcome::Correct;
    }

    nMiss.inc(doStats && doUpdate && dinst->has_stats());
    return Outcome::Miss;
  }

  I(doUpdate);

  // The branch is taken. Update the cache

  if (dinst->getInst()->doesCtrl2Label() && btbicache) {
    nHitLabel.inc(doStats && doUpdate && dinst->has_stats());
    return Outcome::Correct;
  }

  BTBCache::CacheLine *cl = data->fillLine(key, 0xdeaddead);
  I(cl);

  Addr_t predictID = cl->inst;
  cl->inst         = dinst->getAddr();

  if (predictID == dinst->getAddr()) {
    nHit.inc(doStats && doUpdate && dinst->has_stats());
    return Outcome::Correct;
  }

  nMiss.inc(doStats && doUpdate && dinst->has_stats());
  return Outcome::NoBTB;
}

/*****************************************
 * BPOracle
 */

Outcome BPOracle::predict(Dinst *dinst, bool doUpdate, bool doStats) {
  if (!dinst->isTaken()) {
    return Outcome::Correct;  // NT
  }

  return btb.predict(dinst, doUpdate, doStats);
}

/*****************************************
 * BPTaken
 */

Outcome BPTaken::predict(Dinst *dinst, bool doUpdate, bool doStats) {
  if (dinst->getInst()->isJump() || dinst->isTaken()) {
    return btb.predict(dinst, doUpdate, doStats);
  }

  Outcome p = btb.predict(dinst, false, doStats);

  if (p == Outcome::Correct) {
    return Outcome::Correct;  // NotTaken and BTB empty
  }

  return Outcome::Miss;
}

/*****************************************
 * BPNotTaken
 */

Outcome BPNotTaken::predict(Dinst *dinst, bool doUpdate, bool doStats) {
  if (dinst->getInst()->isJump()) {
    return btb.predict(dinst, doUpdate, doStats);
  }

  return dinst->isTaken() ? Outcome::Miss : Outcome::Correct;
}

/*****************************************
 * BPMiss
 */

Outcome BPMiss::predict(Dinst *dinst, bool doUpdate, bool doStats) {
  (void)dinst;
  (void)doUpdate;
  (void)doStats;
  return Outcome::Miss;
}

/*****************************************
 * BPNotTakenEnhaced
 */

Outcome BPNotTakenEnhanced::predict(Dinst *dinst, bool doUpdate, bool doStats) {
  if (dinst->getInst()->isJump()) {
    return btb.predict(dinst, doUpdate, doStats);  // backward branches predicted as taken (loops)
  }

  if (dinst->isTaken()) {
    Addr_t dest = dinst->getAddr();
    if (dest < dinst->getPC()) {
      return btb.predict(dinst, doUpdate, doStats);  // backward branches predicted as taken (loops)
    }

    return Outcome::Miss;  // Forward branches predicted as not-taken, but it was taken
  }

  return Outcome::Correct;
}

/*****************************************
 * BP2bit
 */

BP2bit::BP2bit(int32_t i, const std::string &section, const std::string &sname)
    : BPred(i, section, sname, "2bit")
    , btb(i, section, sname)
    , table(section, Config::get_power2(section, "size", 1), Config::get_integer(section, "bits", 1, 7)) {}

Outcome BP2bit::predict(Dinst *dinst, bool doUpdate, bool doStats) {
  if (dinst->getInst()->isJump()) {
    return btb.predict(dinst, doUpdate, doStats);
  }

  bool taken = dinst->isTaken();
  bool ptaken;
  if (doUpdate) {
    ptaken = table.predict(calcHist(dinst->getPC()), taken);
  } else {
    ptaken = table.predict(calcHist(dinst->getPC()));
  }

#if 0
  if (dinst->has_stats())
    printf("bp2bit %llx %llx %s\n",dinst->getPC(), calcHist(dinst->getPC()) & 32767,taken?"T":"NT");
#endif

  if (taken != ptaken) {
    if (doUpdate) {
      btb.updateOnly(dinst);
    }
    return Outcome::Miss;
  }

  return ptaken ? btb.predict(dinst, doUpdate, doStats) : Outcome::Correct;
}

/*****************************************
 * BPTLdbp
 */

BPLdbp::BPLdbp(int32_t i, const std::string &section, const std::string &sname, MemObj *dl1)
    : BPred(i, section, sname, "ldbp"), btb(i, section, sname), DOC_SIZE(Config::get_power2(section, "doc_size")) {
  DL1 = dl1;
}

Outcome BPLdbp::predict(Dinst *dinst, bool doUpdate, bool doStats) {
#if 1
  if (dinst->getInst()->getOpcode() != Opcode::iBALU_LBRANCH) {  // don't bother about jumps and calls
    return Outcome::None;
  }
#endif
  // Not even faster branch
  // if(dinst->getInst()->isJump())
  //   return btb.predict(dinst, doUpdate, doStats);

  bool       ptaken;
  const bool taken = dinst->isTaken();
  if (dinst->isUseLevel3()) {
    ptaken = taken;
  } else {
    return Outcome::None;
  }

  if (taken != ptaken) {
    if (doUpdate) {
      btb.updateOnly(dinst);
    }
    // tDataTable.update(t_tag, taken); //update needed here?
    return Outcome::Miss;  // FIXME: maybe get numbers with magic to see the limit
  }

  return ptaken ? btb.predict(dinst, doUpdate, doStats) : Outcome::Correct;
}

#if 0
BrOpType BPLdbp::branch_type(Addr_t br_pc) {
  uint64_t raw_op      = 0xdeadcafe; // esesc_mem_read(br_pc);
  uint8_t  br_opcode   = raw_op & 3;
  uint8_t  get_br_bits = (raw_op >> 12) & 7;  // extract bits 12 to 14 to get Br Type
  if (br_opcode == 3) {
    switch (get_br_bits) {
      case BEQ: return BEQ;
      case BNE: return BNE;
      case BLT: return BLT;
      case BGE: return BGE;
      case BLTU: return BLTU;
      case BGEU: return BGEU;
      default: I(false);  // "ILLEGAL_BR=%llx OP_TYPE:%u", br_pc, get_br_bits
    }
  } else if (br_opcode == 1) {
    if (get_br_bits == 4 || get_br_bits == 5) {
      return BEQ;
    } else if (get_br_bits == 6 || get_br_bits == 7) {
      return BNE;
    }
  }
  return ILLEGAL_BR;
}
#endif

bool BPLdbp::outcome_calculator(BrOpType br_op, Data_t br_data1, Data_t br_data2) {
  if (br_op == BEQ) {
    if ((int)br_data1 == (int)br_data2) {
      return 1;
    }
    return 0;
  } else if (br_op == BNE) {
    if ((int)br_data1 != (int)br_data2) {
      return 1;
    }
    return 0;
  } else if (br_op == BLT) {
    if ((int)br_data1 < (int)br_data2) {
      return 1;
    }
    return 0;
  } else if (br_op == BLTU) {
    if (br_data1 < br_data2) {
      return 1;
    }
    return 0;
  } else if (br_op == BGE) {
    if ((int)br_data1 >= (int)br_data2) {
      return 1;
    }
    return 0;
  } else if (br_op == BGEU) {
    if (br_data1 >= br_data2) {
      return 1;
    }
    return 0;
  }

  return 0;  // default is "not taken"
}

/*****************************************
 * BPTData
 */

BPTData::BPTData(int32_t i, const std::string &section, const std::string &sname)
    : BPred(i, section, sname, "tdata")
    , btb(i, section, sname)
    , tDataTable(section, Config::get_power2(section, "size", 1), Config::get_integer(section, "bits", 1, 7)) {}

Outcome BPTData::predict(Dinst *dinst, bool doUpdate, bool doStats) {
  if (dinst->getInst()->isJump()) {
    return btb.predict(dinst, doUpdate, doStats);
  }

  bool   taken  = dinst->isTaken();
  Addr_t old_pc = dinst->getPC();
  bool   ptaken;
  Addr_t t_tag = dinst->getLDPC() ^ (old_pc << 7) ^ (dinst->getDataSign() << 10);
  // Addr_t t_tag = dinst->getPC() ^ dinst->getLDPC() ^ dinst->getDataSign();

  if (doUpdate) {
    ptaken = tDataTable.predict(t_tag, taken);
  } else {
    ptaken = tDataTable.predict(t_tag);
  }

  if (!tDataTable.isLowest(t_tag) && !tDataTable.isHighest(t_tag)) {
    return Outcome::None;  // Only if Highly confident
  }

  if (taken != ptaken) {
    if (doUpdate) {
      btb.updateOnly(dinst);
    }
    // tDataTable.update(t_tag, taken); //update needed here?
    return Outcome::Miss;
  }

  return ptaken ? btb.predict(dinst, doUpdate, doStats) : Outcome::Correct;
}

/*****************************************
 * BPIMLI: SC-TAGE-L with IMLI from Seznec Micro paper
 */

BPIMLI::BPIMLI(int32_t i, const std::string &section, const std::string &sname)
    : BPred(i, section, sname, "imli"), btb(i, section, sname), FetchPredict(Config::get_bool(section, "fetch_predict")) {
  int FetchWidth = Config::get_power2("soc", "core", i, "fetch_width", 1);

  int bimodalSize = Config::get_power2(section, "bimodal_size", 4);
  int bwidth      = Config::get_integer(section, "bimodal_width");

  int log2fetchwidth = log2(FetchWidth);
  int blogb          = log2(bimodalSize) - log2(FetchWidth);

  int nhist = Config::get_integer(section, "nhist", 1);

  bool statcorrector = Config::get_bool(section, "statcorrector");

  imli = std::make_unique<IMLIBest>(log2fetchwidth, blogb, bwidth, nhist, statcorrector);
}

void BPIMLI::fetchBoundaryBegin(Dinst *dinst) {
  if (FetchPredict) {
    imli->fetchBoundaryBegin(dinst->getPC());
  }
}

void BPIMLI::fetchBoundaryEnd() {
  if (FetchPredict) {
    imli->fetchBoundaryEnd();
  }
}

Outcome BPIMLI::predict(Dinst *dinst, bool doUpdate, bool doStats) {
  if (dinst->getInst()->isJump() || dinst->getInst()->isFuncRet()) {
    imli->TrackOtherInst(dinst->getPC(), dinst->getInst()->getOpcode(), dinst->getAddr());
    dinst->setBiasBranch(true);
    return btb.predict(dinst, doUpdate, doStats);
  }

  bool taken = dinst->isTaken();

  if (!FetchPredict) {
    imli->fetchBoundaryBegin(dinst->getPC());
  }

  bool bias;
  // bool     bias = false;
  Addr_t   pc     = dinst->getPC();
  uint32_t sign   = 0;
  bool     ptaken = imli->getPrediction(pc, bias, sign);  // pass taken for statistics
  dinst->setBiasBranch(bias);
  dinst->setBranchSignature(sign);

  bool no_alloc = true;
  if (dinst->isUseLevel3()) {
    no_alloc = false;
  }

  if (doUpdate) {
    imli->updatePredictor(pc, taken, ptaken, dinst->getAddr(), no_alloc);
  }

  if (!FetchPredict) {
    imli->fetchBoundaryEnd();
  }

  if (taken != ptaken) {
    if (doUpdate) {
      btb.updateOnly(dinst);
    }
    return Outcome::Miss;
  }

  return ptaken ? btb.predict(dinst, doUpdate, doStats) : Outcome::Correct;
}


//class PREDICTOR;

BPSuperbp::BPSuperbp(int32_t i, const std::string &section, const std::string &sname): BPred(i, section, sname, "superbp"), btb(i, section, sname), FetchPredict(Config::get_bool(section, "fetch_predict")) {
// TODO
  /*
  int FetchWidth = Config::get_power2("soc", "core", i, "fetch_width", 1);
  
  int bimodalSize = Config::get_power2(section, "bimodal_size", 4);
  int bwidth      = Config::get_integer(section, "bimodal_width");
    int blogb          = log2(bimodalSize) - log2(FetchWidth);

  int nhist = Config::get_integer(section, "nhist", 1);
 
  int log2fetchwidth = log2(FetchWidth);
  */
  
  /*int SBP_NUMG = Config::get_integer(section, "SBP_NUMG");
  int LOG2FETCHWIDTH = Config::get_integer(section, "LOG2FETCHWIDTH");
  int NUM_TAKEN_BRANCHES = Config::get_integer(section, "NUM_TAKEN_BRANCHES");*/
  
  //superbp_p = std::make_unique<PREDICTOR>(SBP_NUMG, LOG2FETCHWIDTH, NUM_TAKEN_BRANCHES);
  superbp_p = std::make_unique<PREDICTOR>();
}

void BPSuperbp::fetchBoundaryBegin (Dinst *dinst) {
  if (FetchPredict) {
    superbp_p->fetchBoundaryBegin(dinst->getPC());
  }
}

void BPSuperbp::fetchBoundaryEnd () {
  if (FetchPredict) {
    superbp_p->fetchBoundaryEnd();
  }
}

  Outcome BPSuperbp::predict (Dinst *dinst, bool doUpdate, bool doStats) {
    //return btb.predict(dinst, doUpdate, doStats);
    uint64_t pc = dinst->getPC();
    uint8_t insn_type = 	dinst->getInst()->isFuncRet	()		? 4 /*insn_t::ret*/ : \
    									dinst->getInst()->isFuncCall()		? 3 /*insn_t::call*/ : \
    									dinst->getInst()->	isBranch()		? 2 /*insn_t::branch*/ : \
    									dinst->getInst()->isJump() 		? 1 /*insn_t::jump*/ : \
    									0; /*insn_t::non_cti;*/
    
    bool taken = dinst->isTaken();
    bool ptaken = superbp_p->handle_insn_t(pc, insn_type, taken);
    // superbp in sync (must be done for all instructions), prediction made, also ftq updated as per previous resolution info
    
    // for jump - may just return btb target
      if (dinst->getInst()->isJump()) {
    return btb.predict(dinst, doUpdate, doStats);
  }
  
  if (taken != ptaken) {
    if (doUpdate) {
      btb.updateOnly(dinst);
    }
    return Outcome::Miss;
  }

  return ptaken ? btb.predict(dinst, doUpdate, doStats) : Outcome::Correct;
	
}

/*
Outcome BPSuperbp::predict(Dinst *dinst, bool doUpdate, bool doStats) {
  if (dinst->getInst()->isJump() || dinst->getInst()->isFuncRet()) {
    uint64_t branchTarget = 0; // Check, get it somehow
    superbp_p->TrackOtherInst(dinst->getPC(), true, branchTarget); // Check
    dinst->setBiasBranch(true);
    return btb.predict(dinst, doUpdate, doStats);
  }

  bool taken = dinst->isTaken();

  if (!FetchPredict) {
    superbp_p->fetchBoundaryBegin(dinst->getPC());
  }

  bool bias;
  // bool     bias = false;
  Addr_t   pc     = dinst->getPC();
  uint32_t sign   = 0;
  bool     ptaken = superbp_p->GetPrediction(pc);  // pass taken for statistics
  dinst->setBiasBranch(bias);
  dinst->setBranchSignature(sign);

//  bool no_alloc = true;
 // if (dinst->isUseLevel3()) {
//    no_alloc = false;
 // }

  if (doUpdate) {
  uint64_t fetch_pc = 0; // Check, get somehow
  uint8_t offset_within_entry = 0; // Check, get somehow
    superbp_p->Updatetables(pc, fetch_pc, offset_within_entry, taken); // Check - superbp updates tables and history separately
    uint64_t branchTarget = 0; // Check - Take from btb
    superbp_p->Updatehistory(taken, branchTarget);
  }

  if (!FetchPredict) {
    imli->fetchBoundaryEnd();
  }

  if (taken != ptaken) {
    if (doUpdate) {
      btb.updateOnly(dinst);
    }
    return Outcome::Miss;
  }

  return ptaken ? btb.predict(dinst, doUpdate, doStats) : Outcome::Correct;
}
*/


/*****************************************
 * BP2level
 */

BP2level::BP2level(int32_t i, const std::string &section, const std::string &sname)
    : BPred(i, section, sname, "2level")
    , btb(i, section, sname)
    , l1Size(Config::get_power2(section, "l1_size"))
    , l1SizeMask(l1Size - 1)
    , historySize(Config::get_integer(section, "history_size", 1, 63))
    , historyMask((1 << historySize) - 1)
    , globalTable(section, Config::get_power2(section, "l2_size"), Config::get_integer(section, "l2_width"))
    , dolc(Config::get_integer(section, "history_size"), 5, 2, 2) {
  useDolc = Config::get_bool(section, "path_based");

  I((l1Size & (l1Size - 1)) == 0);

  historyTable = new HistoryType[l1Size * maxCores];
  I(historyTable);
}

BP2level::~BP2level() { delete historyTable; }

Outcome BP2level::predict(Dinst *dinst, bool doUpdate, bool doStats) {
  if (dinst->getInst()->isJump()) {
    if (useDolc) {
      dolc.update(dinst->getPC());
    }
    return btb.predict(dinst, doUpdate, doStats);
  }

  bool        taken   = dinst->isTaken();
  HistoryType iID     = calcHist(dinst->getPC());
  uint16_t    l1Index = iID & l1SizeMask;
  l1Index             = l1Index + dinst->getFlowId() * l1Size;
  I(l1Index < (maxCores * l1Size));

  HistoryType l2Index = historyTable[l1Index];

  if (useDolc) {
    dolc.update(dinst->getPC());
  }

  // update historyTable statistics
  if (doUpdate) {
    HistoryType nhist = 0;
    if (useDolc) {
      nhist = dolc.getSign(historySize, historySize);
    } else {
      nhist = ((l2Index << 1) | ((iID >> 2 & 1) ^ (taken ? 1 : 0))) & historyMask;
    }
    historyTable[l1Index] = nhist;
  }

  // calculate Table possition
  l2Index = ((l2Index ^ iID) & historyMask) | (iID << historySize);

  if (useDolc && taken) {
    dolc.update(dinst->getAddr());
  }

  bool ptaken;
  if (doUpdate) {
    ptaken = globalTable.predict(l2Index, taken);
  } else {
    ptaken = globalTable.predict(l2Index);
  }

  if (taken != ptaken) {
    if (doUpdate) {
      btb.updateOnly(dinst);
    }
    return Outcome::Miss;
  }

  return ptaken ? btb.predict(dinst, doUpdate, doStats) : Outcome::Correct;
}

/*****************************************
 * BPHybid
 */

BPHybrid::BPHybrid(int32_t i, const std::string &section, const std::string &sname)
    : BPred(i, section, sname, "Hybrid")
    , btb(i, section, sname)
    , historySize(Config::get_power2(section, "history_size"))
    , historyMask((1 << historySize) - 1)
    , globalTable(section, Config::get_power2(section, "global_size", 4), Config::get_integer(section, "global_width", 1, 7))
    , ghr(0)
    , localTable(section, Config::get_power2(section, "local_size", 4), Config::get_integer(section, "local_width", 1, 7))
    , metaTable(section, Config::get_power2(section, "meta_size", 4), Config::get_integer(section, "meta_width", 1, 7))

{}

BPHybrid::~BPHybrid() {}

Outcome BPHybrid::predict(Dinst *dinst, bool doUpdate, bool doStats) {
  if (dinst->getInst()->isJump()) {
    return btb.predict(dinst, doUpdate, doStats);
  }

  bool        taken   = dinst->isTaken();
  HistoryType iID     = calcHist(dinst->getPC());
  HistoryType l2Index = ghr;

  // update historyTable statistics
  if (doUpdate) {
    ghr = ((ghr << 1) | ((iID >> 2 & 1) ^ (taken ? 1 : 0))) & historyMask;
  }

  // calculate Table possition
  l2Index = ((l2Index ^ iID) & historyMask) | (iID << historySize);

  bool globalTaken;
  bool localTaken;
  if (doUpdate) {
    globalTaken = globalTable.predict(l2Index, taken);
    localTaken  = localTable.predict(iID, taken);
  } else {
    globalTaken = globalTable.predict(l2Index);
    localTaken  = localTable.predict(iID);
  }

  bool metaOut;
  if (!doUpdate) {
    metaOut = metaTable.predict(l2Index);  // do not update meta
  } else if (globalTaken == taken && localTaken != taken) {
    // global is correct, local incorrect
    metaOut = metaTable.predict(l2Index, false);
  } else if (globalTaken != taken && localTaken == taken) {
    // global is incorrect, local correct
    metaOut = metaTable.predict(l2Index, true);
  } else {
    metaOut = metaTable.predict(l2Index);  // do not update meta
  }

  bool ptaken = metaOut ? localTaken : globalTaken;

  if (taken != ptaken) {
    if (doUpdate) {
      btb.updateOnly(dinst);
    }
    return Outcome::Miss;
  }

  return ptaken ? btb.predict(dinst, doUpdate, doStats) : Outcome::Correct;
}

/*****************************************
 * 2BcgSkew
 *
 * Based on:
 *
 * "De-aliased Hybird Branch Predictors" from A. Seznec and P. Michaud
 *
 * "Design Tradeoffs for the Alpha EV8 Conditional Branch Predictor"
 * A. Seznec, S. Felix, V. Krishnan, Y. Sazeides
 */

BP2BcgSkew::BP2BcgSkew(int32_t i, const std::string &section, const std::string &sname)
    : BPred(i, section, sname, "2BcgSkew")
    , btb(i, section, sname)
    , BIM(section, Config::get_power2(section, "bimodal_size", 4))
    , G0(section, Config::get_power2(section, "g0_size", 4))
    , G0HistorySize(Config::get_integer(section, "g0_history_size", 1))
    , G0HistoryMask((1 << G0HistorySize) - 1)
    , G1(section, Config::get_power2(section, "g1_size", 4))
    , G1HistorySize(Config::get_integer(section, "g1_history_size", 1))
    , G1HistoryMask((1 << G1HistorySize) - 1)
    , metaTable(section, Config::get_power2(section, "meta_size", 4))
    , MetaHistorySize(Config::get_integer(section, "meta_history_size", 1))
    , MetaHistoryMask((1 << MetaHistorySize) - 1) {
  history = 0x55555555;
}

BP2BcgSkew::~BP2BcgSkew() {
  // Nothing?
}

Outcome BP2BcgSkew::predict(Dinst *dinst, bool doUpdate, bool doStats) {
  if (dinst->getInst()->isJump()) {
    return btb.predict(dinst, doUpdate, doStats);
  }

  HistoryType iID = calcHist(dinst->getPC());

  bool taken = dinst->isTaken();

  HistoryType xorKey1 = history ^ iID;
  HistoryType xorKey2 = history ^ (iID >> 2);
  HistoryType xorKey3 = history ^ (iID >> 4);

  HistoryType metaIndex = (xorKey1 & MetaHistoryMask) | iID << MetaHistorySize;
  HistoryType G0Index   = (xorKey2 & G0HistoryMask) | iID << G0HistorySize;
  HistoryType G1Index   = (xorKey3 & G1HistoryMask) | iID << G1HistorySize;

  bool metaOut = metaTable.predict(metaIndex);

  bool BIMOut = BIM.predict(iID);
  bool G0Out  = G0.predict(G0Index);
  bool G1Out  = G1.predict(G1Index);

  bool gskewOut = (G0Out ? 1 : 0) + (G1Out ? 1 : 0) + (BIMOut ? 1 : 0) >= 2;

  bool ptaken = metaOut ? BIMOut : gskewOut;

  if (ptaken != taken) {
    if (!doUpdate) {
      return Outcome::Miss;
    }

    BIM.predict(iID, taken);
    G0.predict(G0Index, taken);
    G1.predict(G1Index, taken);

    BIMOut = BIM.predict(iID);
    G0Out  = G0.predict(G0Index);
    G1Out  = G1.predict(G1Index);

    gskewOut = (G0Out ? 1 : 0) + (G1Out ? 1 : 0) + (BIMOut ? 1 : 0) >= 2;
    if (BIMOut != gskewOut) {
      metaTable.predict(metaIndex, (BIMOut == taken));
    } else {
      metaTable.reset(metaIndex, (BIMOut == taken));
    }

    I(doUpdate);
    btb.updateOnly(dinst);
    return Outcome::Miss;
  }

  if (doUpdate) {
    if (metaOut) {
      BIM.predict(iID, taken);
    } else {
      if (BIMOut == taken) {
        BIM.predict(iID, taken);
      }
      if (G0Out == taken) {
        G0.predict(G0Index, taken);
      }
      if (G1Out == taken) {
        G1.predict(G1Index, taken);
      }
    }

    if (BIMOut != gskewOut) {
      metaTable.predict(metaIndex, (BIMOut == taken));
    }

    history = history << 1 | ((iID >> 2 & 1) ^ (taken ? 1 : 0));
  }

  return ptaken ? btb.predict(dinst, doUpdate, doStats) : Outcome::Correct;
}

/*****************************************
 * YAGS
 *
 * Based on:
 *
 * "The YAGS Brnach Prediction Scheme" by A. N. Eden and T. Mudge
 *
 * Arguments to the predictor:
 *     type    = "yags"
 *     l1size  = (in power of 2) Taken Cache Size.
 *     l2size  = (in power of 2) Not-Taken Cache Size.
 *     l1bits  = Number of bits for Cache Taken Table counter (2).
 *     l2bits  = Number of bits for Cache NotTaken Table counter (2).
 *     size    = (in power of 2) Size of the Choice predictor.
 *     bits    = Number of bits for Choice predictor Table counter (2).
 *     tagbits = Number of bits used for storing the address in
 *               direction cache.
 *
 * Description:
 *
 * This predictor tries to address the conflict aliasing in the choice
 * predictor by having two direction caches. Depending on the
 * prediction, the address is looked up in the opposite direction and if
 * there is a cache hit then that predictor is used otherwise the choice
 * predictor is used. The choice predictor and the direction predictor
 * used are updated based on the outcome.
 *
 */

BPyags::BPyags(int32_t i, const std::string &section, const std::string &sname)
    : BPred(i, section, sname, "yags")
    , btb(i, section, sname)
    , historySize(24)
    , historyMask((1 << 24) - 1)
    , table(section, Config::get_power2(section, "meta_size", 4), Config::get_power2(section, "meta_width", 1, 7))
    , ctableTaken(section, Config::get_power2(section, "l1_size", 4), Config::get_power2(section, "l1_width", 1, 7))
    , ctableNotTaken(section, Config::get_power2(section, "l2_size", 4), Config::get_power2(section, "l2_width", 1, 7)) {
  CacheTaken        = new uint8_t[Config::get_power2(section, "l1_size")];
  CacheTakenMask    = Config::get_power2(section, "l1_size") - 1;
  CacheTakenTagMask = (1 << Config::get_integer(section, "l_tag_width")) - 1;

  CacheNotTaken        = new uint8_t[Config::get_power2(section, "l2_size")];
  CacheNotTakenMask    = Config::get_power2(section, "l2_size") - 1;
  CacheNotTakenTagMask = (1 << Config::get_integer(section, "l_tag_width")) - 1;
}

BPyags::~BPyags() {}

Outcome BPyags::predict(Dinst *dinst, bool doUpdate, bool doStats) {
  if (dinst->getInst()->isJump()) {
    return btb.predict(dinst, doUpdate, doStats);
  }

  bool        taken   = dinst->isTaken();
  HistoryType iID     = calcHist(dinst->getPC());
  HistoryType iIDHist = ghr;
  bool        choice;
  if (doUpdate) {
    ghr    = ((ghr << 1) | ((iID >> 2 & 1) ^ (taken ? 1 : 0))) & historyMask;
    choice = table.predict(iID, taken);
  } else {
    choice = table.predict(iID);
  }

  iIDHist = ((iIDHist ^ iID) & historyMask) | (iID << historySize);

  bool ptaken;
  if (choice) {
    ptaken = true;

    // Search the not taken cache. If we find an entry there, the
    // prediction from the cache table will override the choice table.

    HistoryType cacheIndex = iIDHist & CacheNotTakenMask;
    HistoryType tag        = iID & CacheNotTakenTagMask;
    bool        cacheHit   = (CacheNotTaken[cacheIndex] == tag);

    if (cacheHit) {
      if (doUpdate) {
        CacheNotTaken[cacheIndex] = tag;
        ptaken                    = ctableNotTaken.predict(iIDHist, taken);
      } else {
        ptaken = ctableNotTaken.predict(iIDHist);
      }
    } else if ((doUpdate) && (taken == false)) {
      CacheNotTaken[cacheIndex] = tag;
      (void)ctableNotTaken.predict(iID, taken);
    }
  } else {
    ptaken = false;
    // Search the taken cache. If we find an entry there, the prediction
    // from the cache table will override the choice table.

    HistoryType cacheIndex = iIDHist & CacheTakenMask;
    HistoryType tag        = iID & CacheTakenTagMask;
    bool        cacheHit   = (CacheTaken[cacheIndex] == tag);

    if (cacheHit) {
      if (doUpdate) {
        CacheTaken[cacheIndex] = tag;
        ptaken                 = ctableTaken.predict(iIDHist, taken);
      } else {
        ptaken = ctableTaken.predict(iIDHist);
      }
    } else if ((doUpdate) && (taken == true)) {
      CacheTaken[cacheIndex] = tag;
      (void)ctableTaken.predict(iIDHist, taken);
      ptaken = false;
    }
  }

  if (taken != ptaken) {
    if (doUpdate) {
      btb.updateOnly(dinst);
    }
    return Outcome::Miss;
  }

  return ptaken ? btb.predict(dinst, doUpdate, doStats) : Outcome::Correct;
}

/*****************************************
 * BPOgehl
 *
 * Based on "The O-GEHL Branch Predictor" by Andre Seznec
 * Code ported from Andre's CBP 2004 entry by Jay Boice (boice@soe.ucsc.edu)
 *
 */

BPOgehl::BPOgehl(int32_t i, const std::string &section, const std::string &sname)
    : BPred(i, section, sname, "ogehl")
    , btb(i, section, sname)
    , mtables(Config::get_integer(section, "num_tables", 3, 32))
    , max_history_size(Config::get_integer(section, "max_history_size", 8, 1024))
    , nentry(3)
    , addwidth(8)
    , logpred(log2i(Config::get_power2(section, "table_size")))
    , THETA(Config::get_integer(section, "num_tables"))
    , MAXTHETA(31)
    , THETAUP(1 << (Config::get_integer(section, "table_cbits", 1, 15) - 1))
    , PREDUP(1 << (Config::get_integer(section, "table_width", 1, 15) - 1))
    , TC(0) {
  pred = new char *[mtables];
  for (int32_t t = 0; t < mtables; t++) {
    pred[t] = new char[1 << logpred];
    for (int32_t j = 0; j < (1 << logpred); j++) {
      pred[t][j] = 0;
    }
  }

  T       = new int[nentry * logpred + 1];
  ghist   = new int64_t[(max_history_size >> 6) + 1];
  MINITAG = new uint8_t[(1 << (logpred - 1))];

  for (int32_t h = 0; h < (max_history_size >> 6) + 1; h++) {
    ghist[h] = 0;
  }

  for (int32_t j = 0; j < (1 << (logpred - 1)); j++) {
    MINITAG[j] = 0;
  }
  AC = 0;

  double initset = 3;
  double tt      = ((double)max_history_size) / initset;
  double Pow     = pow(tt, 1.0 / (mtables + 1));

  histLength     = new int[mtables + 3];
  usedHistLength = new int[mtables];
  histLength[0]  = 0;
  histLength[1]  = 3;
  for (int32_t t = 2; t < mtables + 3; t++) {
    histLength[t] = (int)((initset * pow(Pow, (double)(t - 1))) + 0.5);
  }
  for (int32_t t = 0; t < mtables; t++) {
    usedHistLength[t] = histLength[t];
  }
}

BPOgehl::~BPOgehl() {}

Outcome BPOgehl::predict(Dinst *dinst, bool doUpdate, bool doStats) {
  if (dinst->getInst()->isJump()) {
    return btb.predict(dinst, doUpdate, doStats);
  }

  bool taken  = dinst->isTaken();
  bool ptaken = false;

  int32_t      S   = 0;  // mtables/2
  HistoryType *iID = (HistoryType *)alloca(mtables * sizeof(HistoryType));

  // Prediction is sum of entries in M tables (table 1 is half-size to fit in 64k)
  for (int32_t t = 0; t < mtables; t++) {
    if (t == 1) {
      logpred--;
    }
    iID[t] = geoidx(dinst->getPC() >> 2, ghist, usedHistLength[t], (t & 3) + 1);
    if (t == 1) {
      logpred++;
    }
    S += pred[t][iID[t]];
  }
  ptaken = (S >= 0);

  if (doUpdate) {
    // Update theta (threshold)
    if (taken != ptaken) {
      TC++;
      if (TC > THETAUP - 1) {
        TC = THETAUP - 1;
        if (THETA < MAXTHETA) {
          TC = 0;
          THETA++;
        }
      }
    } else if (S < THETA && S >= -THETA) {
      TC--;
      if (TC < -THETAUP) {
        TC = -THETAUP;
        if (THETA > 0) {
          TC = 0;
          THETA--;
        }
      }
    }

    if (taken != ptaken || (S < THETA && S >= -THETA)) {
      // Update M tables
      for (int32_t t = 0; t < mtables; t++) {
        if (taken) {
          if (pred[t][iID[t]] < PREDUP - 1) {
            pred[t][iID[t]]++;
          }
        } else {
          if (pred[t][iID[t]] > -PREDUP) {
            pred[t][iID[t]]--;
          }
        }
      }

      // Update history lengths
      if (taken != ptaken) {
        miniTag = MINITAG[iID[mtables - 1] >> 1];
        if (miniTag != genMiniTag(dinst)) {
          AC -= 4;
          if (AC < -256) {
            AC                = -256;
            usedHistLength[6] = histLength[6];
            usedHistLength[4] = histLength[4];
            usedHistLength[2] = histLength[2];
          }
        } else {
          AC++;
          if (AC > 256 - 1) {
            AC                = 256 - 1;
            usedHistLength[6] = histLength[mtables + 2];
            usedHistLength[4] = histLength[mtables + 1];
            usedHistLength[2] = histLength[mtables];
          }
        }
      }
      MINITAG[iID[mtables - 1] >> 1] = genMiniTag(dinst);
    }

    // Update branch/path histories
    for (int32_t i = (max_history_size >> 6) + 1; i > 0; i--) {
      ghist[i] = (ghist[i] << 1) + (ghist[i - 1] < 0);
    }
    ghist[0] = ghist[0] << 1;
    if (taken) {
      ghist[0] = 1;
    }
#if 0
    static int conta = 0;
    conta++;
    if (conta > max_history_size) {
      conta = 0;
      printf("@%lld O:",globalClock);
      uint64_t start_mask = max_history_size&63;
      start_mask          = 1<<start_mask;
      for (int32_t i = (max_history_size >> 6)+1; i > 0; i--) {
        for (uint64_t j=start_mask;j!=0;j=j>>1) {
          if (ghist[i] & j) {
            printf("1");
          }else{
            printf("0");
          }
        }
        start_mask=((uint64_t) 1)<<63;
        //printf(":");
      }
      printf("\n");
    }
#endif
  }

  if (taken != ptaken) {
    if (doUpdate) {
      btb.updateOnly(dinst);
    }
    return Outcome::Miss;
  }

  return ptaken ? btb.predict(dinst, doUpdate, doStats) : Outcome::Correct;
}

int32_t BPOgehl::geoidx(uint64_t Add, int64_t *histo, int32_t m, int32_t funct) {
  uint64_t inter, Hh, Res;
  int32_t  x, i, shift;
  int32_t  PT;
  int32_t  MinAdd;
  int32_t  FUNCT;

  MinAdd = nentry * logpred - m;
  if (MinAdd > 20) {
    MinAdd = 20;
  }

  if (MinAdd >= 8) {
    inter = ((histo[0] & ((1 << m) - 1)) << (MinAdd)) + ((Add & ((1 << MinAdd) - 1)));
  } else {
    for (x = 0; x < nentry * logpred; x++) {
      T[x] = ((x * (addwidth + m - 1)) / (nentry * logpred - 1));
    }

    T[nentry * logpred] = addwidth + m;
    inter               = 0;

    Hh = histo[0];
    Hh >>= T[0];
    inter = (Hh & 1);
    PT    = 1;

    for (i = 1; T[i] < m; i++) {
      if ((T[i] & 0xffc0) == (T[i - 1] & 0xffc0)) {
        shift = T[i] - T[i - 1];
      } else {
        Hh = histo[PT];
        PT++;
        shift = T[i] & 63;
      }

      inter = (inter << 1);
      Hh    = Hh >> shift;
      inter ^= (Hh & 1);
    }

    Hh = Add;
    for (; T[i] < m + addwidth; i++) {
      shift = T[i] - m;
      inter = (inter << 1);
      inter ^= ((Hh >> shift) & 1);
    }
  }

  FUNCT = funct;
  Res   = inter & ((1 << logpred) - 1);
  for (i = 1; i < nentry; i++) {
    inter = inter >> logpred;
    Res ^= ((inter & ((1 << logpred) - 1)) >> FUNCT) ^ ((inter & ((1 << FUNCT) - 1)) << ((logpred - FUNCT)));
    FUNCT = (FUNCT + 1) % logpred;
  }

  return ((int)Res);
}

/*****************************************
 * LGW: Local Global Wavelet
 *
 * Extending OGEHL with TAGE ideas and LGW
 *
 */

LoopPredictor::LoopPredictor(int n) : nentries(n) { table = new LoopEntry[nentries]; }

void LoopPredictor::update(uint64_t key, uint64_t tag, bool taken) {
  // FIXME: add a small learning fully assoc (12 entry?) to learn loops. Backup with a 2-way loop entry
  //
  // FIXME: add some resilience to have loops that alternate between different loop sizes.
  //
  // FIXME: systematically check all the loops in CBP, and see how to capture them all

  LoopEntry *ent = &table[key % nentries];

  if (ent->tag != tag) {
    ent->tag         = tag;
    ent->confidence  = 0;
    ent->currCounter = 0;
    ent->dir         = taken;
  }

  ent->currCounter++;
  if (ent->dir != taken) {
    if (ent->iterCounter == ent->currCounter) {
      ent->confidence++;
    } else {
      ent->tag        = 0;
      ent->confidence = 0;
    }

    if (ent->confidence == 0) {
      ent->dir = taken;
    }

    ent->iterCounter = ent->currCounter;
    ent->currCounter = 0;
  }
}

bool LoopPredictor::isLoop(uint64_t key, uint64_t tag) const {
  const LoopEntry *ent = &table[key % nentries];

  if (ent->tag != tag) {
    return false;
  }

  return (ent->confidence * ent->iterCounter) > 800 && ent->confidence > 7;
}

bool LoopPredictor::isTaken(uint64_t key, uint64_t tag, bool taken) {
  I(isLoop(key, tag));

  LoopEntry *ent = &table[key % nentries];

  bool dir = ent->dir;
  if ((ent->currCounter + 1) == ent->iterCounter) {
    dir = !ent->dir;
  }

  if (dir != taken) {
    ent->confidence /= 2;
  }

  return dir;
}

uint32_t LoopPredictor::getLoopIter(uint64_t key, uint64_t tag) const {
  const LoopEntry *ent = &table[key % nentries];

  if (ent->tag != tag) {
    return 0;
  }

  return ent->iterCounter;
}

/*****************************************
 * BPredictor
 */

std::unique_ptr<BPred> BPredictor::getBPred(int32_t id, const std::string &sec, const std::string &sname, MemObj *DL1) {
  std::unique_ptr<BPred> pred;

  auto type = Config::get_string(sec, "type");
  std::transform(type.begin(), type.end(), type.begin(), [](unsigned char c) { return std::tolower(c); });

  // Normal Predictor
  if (type == "oracle") {
    pred = std::make_unique<BPOracle>(id, sec, sname);
  } else if (type == "miss") {
    pred = std::make_unique<BPMiss>(id, sec, sname);
  } else if (type == "not_taken") {
    pred = std::make_unique<BPNotTaken>(id, sec, sname);
  } else if (type == "not_taken_enhanced") {
    pred = std::make_unique<BPNotTakenEnhanced>(id, sec, sname);
  } else if (type == "taken") {
    pred = std::make_unique<BPTaken>(id, sec, sname);
  } else if (type == "2bit") {
    pred = std::make_unique<BP2bit>(id, sec, sname);
  } else if (type == "2level") {
    pred = std::make_unique<BP2level>(id, sec, sname);
  } else if (type == "2bcgskew") {
    pred = std::make_unique<BP2BcgSkew>(id, sec, sname);
  } else if (type == "hybrid") {
    pred = std::make_unique<BPHybrid>(id, sec, sname);
  } else if (type == "yags") {
    pred = std::make_unique<BPyags>(id, sec, sname);
  } else if (type == "imli") {
    pred = std::make_unique<BPIMLI>(id, sec, sname);
  } else if (type == "superbp") {
    pred = std::make_unique<BPSuperbp>(id, sec, sname);
  } else if (type == "tdata") {
    pred = std::make_unique<BPTData>(id, sec, sname);
  } else if (type == "ldbp") {
    pred = std::make_unique<BPLdbp>(id, sec, sname, DL1);
  } else {
    Config::add_error(fmt::format("Invalid branch predictor type [{}] in section [{}]", type, sec));
    return nullptr;
  }
  I(pred);

  return pred;
}

BPredictor::BPredictor(int32_t i, MemObj *iobj, MemObj *dobj, std::shared_ptr<BPredictor> bpred)
    : id(i)
    , SMTcopy(bpred != nullptr)
    , il1(iobj)
    , dl1(dobj)
    , nBTAC(fmt::format("P({})_BPred:nBTAC", id))
    , nBranches(fmt::format("P({})_BPred:nBranches", id))
    , nNoPredict(fmt::format("P({})_BPred:nNoPredict", id))
    , nTaken(fmt::format("P({})_BPred:nTaken", id))
    , nMiss(fmt::format("P({})_BPred:nMiss", id))
    , nBranches2(fmt::format("P({})_BPred:nBranches2", id))
    , nTaken2(fmt::format("P({})_BPred:nTaken2", id))
    , nMiss2(fmt::format("P({})_BPred:nMiss2", id))
    , nBranches3(fmt::format("P({})_BPred:nBranches3", id))
    , nNoPredict3(fmt::format("P({})_BPred:nNoPredict3", id))
    , nHit3_miss2(fmt::format("P({})_BPred:nHit3_miss2", id))
    , nTaken3(fmt::format("P({})_BPred:nTaken3", id))
    , nMiss3(fmt::format("P({})_BPred:nMiss3", id))
    , nFixes1(fmt::format("P({})_BPred:nFixes1", id))
    , nFixes2(fmt::format("P({})_BPred:nFixes2", id))
    , nFixes3(fmt::format("P({})_BPred:nFixes3", id))
    , nUnFixes(fmt::format("P({})_BPred:nUnFixes", id))
    , nAgree3(fmt::format("P({})_BPred:nAgree3", id)) {
  auto cpu_section = Config::get_string("soc", "core", id);
  auto ras_section = Config::get_array_string(cpu_section, "bpred", 0);
  ras              = std::make_unique<BPRas>(id, ras_section, "");

  if (bpred) {  // SMT
    FetchWidth = bpred->FetchWidth;
    pred1      = bpred->pred1;
    pred2      = bpred->pred2;
    pred3      = bpred->pred3;
    return;
  }

  FetchWidth = Config::get_integer(cpu_section, "fetch_width");

  pred1 = nullptr;
  pred2 = nullptr;
  pred3 = nullptr;

  int         last_bpred_delay = 0;
  std::string last_bpred_section;

  auto n_bpred = Config::get_array_size(cpu_section, "bpred", 3);  // 3 is the max_size

  bpredDelay1 = 0;
  bpredDelay2 = 0;
  bpredDelay3 = 0;

  for (auto n = 0u; n < n_bpred; ++n) {
    auto bpred_section = Config::get_array_string(cpu_section, "bpred", n);
    auto bpred_delay   = Config::get_integer(bpred_section, "delay", last_bpred_delay + 1);

    if (n == 0) {
      pred1       = getBPred(id, bpred_section, "0");
      bpredDelay1 = bpred_delay;
    } else if (n == 1) {
      pred2       = getBPred(id, bpred_section, "1");
      bpredDelay2 = bpred_delay;
    } else if (n == 2) {
      pred3       = getBPred(id, bpred_section, "2");
      bpredDelay3 = bpred_delay;
    } else {
      I(0);
    }

    last_bpred_delay   = bpred_delay;
    last_bpred_section = bpred_section;
  }
  if (bpredDelay1 == 0) {
    Config::add_error("branch predictor should have a delay > 0");
    return;
  }
  if (bpredDelay2 == 0) {
    bpredDelay2 = bpredDelay1 + 1;
  }

  if (bpredDelay3 == 0) {
    bpredDelay3 = bpredDelay2 + 1;
  }

  I(bpredDelay1 < bpredDelay3);
  I(bpredDelay2 < bpredDelay3);
  I(bpredDelay1 < bpredDelay2);
}

void BPredictor::fetchBoundaryBegin(Dinst *dinst) {
  pred1->fetchBoundaryBegin(dinst);
  if (pred2) {
    pred2->fetchBoundaryBegin(dinst);
  }
  if (pred3 == 0) {
    return;
  }

  pred3->fetchBoundaryBegin(dinst);
}

void BPredictor::fetchBoundaryEnd() {
  pred1->fetchBoundaryEnd();
  if (pred2) {
    pred2->fetchBoundaryEnd();
  }
  if (pred3 == 0) {
    return;
  }
  pred3->fetchBoundaryEnd();
}

Outcome BPredictor::predict1(Dinst *dinst) {
  I(dinst->getInst()->isControl());

  nBranches.inc(dinst->has_stats());
  nTaken.inc(dinst->isTaken() && dinst->has_stats());

  Outcome p = pred1->doPredict(dinst);

  nMiss.inc(p == Outcome::Miss && dinst->has_stats());
  nNoPredict.inc(p == Outcome::None && dinst->has_stats());

  return p;
}

Outcome BPredictor::predict2(Dinst *dinst) {
  I(dinst->getInst()->isControl());

  nBranches2.inc(dinst->has_stats());
  nTaken2.inc(dinst->isTaken() && dinst->has_stats());
  // No RAS in L2

  Outcome p = pred2->doPredict(dinst);

  // nMiss2.inc(p != Outcome::Correct && dinst->has_stats());
  nMiss2.inc(p == Outcome::Miss && dinst->has_stats());

  return p;
}

Outcome BPredictor::predict3(Dinst *dinst) {
  I(dinst->getInst()->isControl());

#if 0
  if (dinst->isBiasBranch())
    return Outcome::None;
#endif

#if 0
  if(dinst->getDataSign() == DS_NoData)
    return Outcome::None;
#endif

#if 0
  if(!dinst->isBranchMiss_level2()) {
    return Outcome::None;
  }
#endif

  nBranches3.inc(dinst->has_stats());
  nTaken3.inc(dinst->isTaken() && dinst->has_stats());
  // No RAS in L2

  Outcome p = pred3->doPredict(dinst);
#if 0
  if(p == Outcome::None)
    return p;
#endif

  // nMiss3.inc(p != Outcome::Correct && dinst->has_stats());
  nMiss3.inc(p == Outcome::Miss && dinst->has_stats());
  nNoPredict3.inc(p == Outcome::None && dinst->has_stats());

  return p;
}

TimeDelta_t BPredictor::predict(Dinst *dinst, bool *fastfix) {
  *fastfix = true;

  Outcome outcome1;
  Outcome outcome2 = Outcome::None;
  Outcome outcome3 = Outcome::None;
  dinst->setBiasBranch(false);

  outcome1 = ras->doPredict(dinst);
  if (outcome1 == Outcome::None) {
    outcome1 = predict1(dinst);
    // outcome2 = outcome1;
    if (pred2) {
      outcome2 = predict2(dinst);
    }
    // outcome3 = outcome2;
    if (pred3) {
      outcome3 = predict3(dinst);
    }
  }

  if (dinst->getInst()->isFuncRet() || dinst->getInst()->isFuncCall()) {
    dinst->setBiasBranch(true);
    ras->tryPrefetch(il1, dinst->has_stats(), 1);
  }

  if (outcome1 == Outcome::Correct && outcome2 == Outcome::Correct && outcome3 == Outcome::Correct) {
    unset_Miss_Pred_Bool();
  } else {
    set_Miss_Pred_Bool();
  }

#if 1
  if (outcome1 != Outcome::Correct) {
    dinst->setBranchMiss_level1();
  } else {
    dinst->setBranchHit_level1();
  }

  if (outcome2 != Outcome::Correct) {
    dinst->setBranchMiss_level2();
  } else {
    dinst->setBranchHit_level2();
    if (outcome3 == Outcome::Miss || outcome3 == Outcome::NoBTB) {
      dinst->setBranch_hit2_miss3();
    }
  }

  if (outcome3 == Outcome::Miss || outcome3 == Outcome::NoBTB) {
    dinst->setBranchMiss_level3();
  } else if (outcome3 == Outcome::Correct) {
    dinst->setBranchHit_level3();
    if (outcome2 != Outcome::Correct) {
      dinst->setBranch_hit3_miss2();
      nHit3_miss2.inc(true);
    }
    //}else {
    //  dinst->setLevel3_Outcome::None();
  }
#endif

  if (outcome1 == Outcome::Correct && outcome2 == Outcome::Correct && outcome3 == Outcome::Correct) {
    if (dinst->isTaken()) {
#ifdef CLOSE_TARGET_OPTIMIZATION
      int distance = dinst->getAddr() - dinst->getPC();

      uint32_t delta = distance / FetchWidth;
      if (delta <= 2 && distance > 0) {
        return delta;
      }
#endif

      return bpredDelay1;
    }
    return 0;
  }

#if 0
  // Only if reads from icache
  if (dinst->getInst()->doesJump2Label()) {
    nBTAC.inc(dinst->has_stats());
    return bpredDelay1;
  }
#endif

  TimeDelta_t bpred_total_delay = 0;

  // outcome == (Outcome::Correct, Outcome::Miss, Outcome::None, Outcome::NoBTB)
  if (outcome1 == Outcome::Correct && outcome2 != Outcome::Miss && outcome3 != Outcome::Miss) {
    nFixes1.inc(dinst->has_stats());
    bpred_total_delay = bpredDelay1;
  } else if (outcome1 != Outcome::Correct && outcome2 == Outcome::Correct && outcome3 != Outcome::Miss) {
    nFixes2.inc(dinst->has_stats());
    bpred_total_delay = bpredDelay2;
  } else if (outcome1 != Outcome::Correct && outcome2 != Outcome::Correct && outcome3 == Outcome::Correct) {
    nFixes3.inc(dinst->has_stats());
    bpred_total_delay = bpredDelay3;
  } else {
    nUnFixes.inc(dinst->has_stats());
    *fastfix          = false;
    bpred_total_delay = 1;  // Anything but zero
  }

  return bpred_total_delay;
}

void BPredictor::dump(const std::string &str) const {
  (void)str;
  // nothing?
}
