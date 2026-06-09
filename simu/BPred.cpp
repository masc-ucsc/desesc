// See LICENSE for details.

#include "bpred.hpp"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

#include <fstream>
#include <ios>
#include <iostream>

#include "config.hpp"
#include "desesc_alloca.h"
#include "fmt/format.h"
#include "imlibest.hpp"
#include "memobj.hpp"
#include "report.hpp"
#include "tahead.hpp"
#include "tahead1.hpp"

// #define CLOSE_TARGET_OPTIMIZATION 1
// #define BTB_TRACE 1

/*****************************************
 * BPred
 */

BPred::BPred(int32_t i, const std::string& sec, const std::string& sname, const std::string& name)
    : id(i)
    , full_name(fmt::format("P({})_BPred{}_{}", i, sname, name))
    , nHit(fmt::format("P({})_BPred{}_{}:nHit", i, sname, name))
    , nMiss(fmt::format("P({})_BPred{}_{}:nMiss", i, sname, name))
    , first_br(fmt::format("P({})_BPred{}_{}:first_br", i, sname, name))
    , first_jump(fmt::format("P({})_BPred{}_{}:first_jump", i, sname, name))
    , first_ret(fmt::format("P({})_BPred{}_{}:first_ret", i, sname, name))
    , first_br_correct(fmt::format("P({})_BPred{}_{}:first_br_correct", i, sname, name))
    , first_jump_correct(fmt::format("P({})_BPred{}_{}:first_jump_correct", i, sname, name))
    , first_ret_correct(fmt::format("P({})_BPred{}_{}:first_ret_correct", i, sname, name)) {
  addrShift = Config::get_integer(sec, "bp_addr_shift");

  maxCores      = Config::get_array_size("soc", "core");
  taken_counter = -1;
}

BPred::~BPred() {}

// No fetch boundary implemented (must be specialized per predictor if supported)
void BPred::fetchBoundaryBegin(Dinst* dinst) {
  (void)dinst;
  taken_counter = 0;
}

void BPred::fetchBoundaryEnd() { taken_counter = -1; }

/*****************************************
 * RAS
 */
BPRas::BPRas(int32_t i, const std::string& section, const std::string& sname)
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

void BPRas::tryPrefetch(MemObj* il1, bool doStats, int degree) {
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

Outcome BPRas::predict(Dinst* dinst, bool doUpdate, bool doStats) {
  // printf("\n\nBPred.cpp::BPRas::predict::Entering predict::dinstID %llu at clock cycle %llu\n", dinst->getID(), globalClock);
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

#ifdef USE_DOLC
    uint64_t phase = 0;
    for (int i = 0; i < 4; i++) {
      int pos = index - i;
      if (pos < 0) {
        pos = RasSize - 1;
      }
      phase = (phase >> 3) ^ stack[index];
    }

    // idolc.setPhase(phase);
#endif

    if (stack[index] == dinst->getAddr() || (stack[index] + 4) == dinst->getAddr() || (stack[index] + 2) == dinst->getAddr()) {
      // std::print("RET: hit  top:{:x} target:{:x}\n", stack[index],dinst->getAddr());
      return Outcome::Correct;
    }

    // std::print("RET: miss top:{:x} target:{:x}\n", stack[index], dinst->getAddr());

    return Outcome::Miss;
  } else if (dinst->getInst()->isFuncCall() && RasSize) {
    if (doUpdate) {
      // std::print("CALL push:{:x}\n", dinst->getPC());
      stack[index] = dinst->getPC();
      index++;

      if (index >= RasSize) {
        index = 0;
      }
    }
  }

  // printf("\n\nBPred.cpp::BPRas::predict::Leavinging predict::dinstID %llu at clock cycle %llu\n", dinst->getID(), globalClock);
  return Outcome::None;
}

/*****************************************
 * BTB
 */
BPBTB::BPBTB(int32_t i, const std::string& section, const std::string& sname, const std::string& name)
    : nHit(fmt::format("P({})_BPred{}_{}:nHit", i, sname, name))
    , nMiss(fmt::format("P({})_BPred{}_{}:nMiss", i, sname, name))
    , nHitLabel(fmt::format("P({})_BPred{}_{}:nHitLabel", i, sname, name))
    , btb_fetch_predict(Config::get_bool(section, "btb_fetch_predict"))
    , btb_tag_offset(Config::get_bool(section, "btb_tag_offset"))
    , btb_tag_hybrid(Config::get_bool(section, "btb_tag_hybrid"))
    , btb_tag_size(Config::get_integer(section, "btb_tag_size"))
    , log2_btb_nsub(Config::has_entry(section, "btb_nsub") ? log2(Config::get_power2(section, "btb_nsub")) : 0)
    , btb_nsub_mask((1 << log2_btb_nsub) - 1)
    , btb_name(fmt::format("{}{}", name, sname)) {
  btbHistorySize = Config::get_integer(section, "btb_history_size");

  if (btb_tag_offset && btb_tag_hybrid) {
    Config::add_error(fmt::format("Section {} has both btb_tag_offset and btb_tag_hybrid, choose one", sname));
  }

  if (btbHistorySize) {
    dolc = new DOLC(btbHistorySize + 1, 5, 2, 2);
  } else {
    dolc = 0;
  }

  btbicache = Config::get_bool(section, "btb_split_il1");

  if (Config::get_integer(section, "btb_size") == 0) {
    // Oracle
    data.clear();
    return;
  }

  boundaryPC        = 0;
  btb_taken_counter = -1;
  btb_tag_counter   = -1;

  uint32_t shift_amt = (sizeof(uint32_t) << 3) - btb_tag_size;
  btb_tag_mask       = ((uint32_t)(-1 << shift_amt)) >> shift_amt;

  int  btb_nsub      = 1 << log2_btb_nsub;
  int  btb_size      = Config::get_power2(section, "btb_size");
  int  btb_assoc     = Config::get_integer(section, "btb_assoc");
  int  btb_line_size = Config::get_power2(section, "btb_line_size", 1, 1024);
  bool btb_xor       = Config::has_entry(section, "btb_xor") ? Config::get_bool(section, "btb_xor") : false;
  bool btb_skew      = Config::has_entry(section, "btb_skew") ? Config::get_bool(section, "btb_skew") : false;
  int  btb_addr_unit
      = Config::has_entry(section, "btb_addrUnit") ? Config::get_power2(section, "btb_addrUnit", 0, btb_line_size) : 1;
  auto repl_policy = Config::get_string(section, "btb_repl_policy");

  int btb_bank_size = btb_size >> log2_btb_nsub;
  if (btb_bank_size < btb_assoc * btb_line_size) {
    Config::add_error(
        fmt::format("Section {} has btb_size {} too small for btb_nsub {} and btb_assoc {}", sname, btb_size, btb_nsub, btb_assoc));
  }

  data.resize(btb_nsub, nullptr);
  for (int bank = 0; bank < btb_nsub; ++bank) {
    data[bank] = BTBCache::create(btb_bank_size, btb_assoc, btb_line_size, btb_addr_unit, repl_policy, btb_skew, btb_xor);
    I(data[bank]);
  }
}

BPBTB::~BPBTB() {
  for (auto* bank : data) {
    if (bank) {
      bank->destroy();
    }
  }
}

void BPBTB::fetchBoundaryBegin(Dinst* dinst) {
  boundaryPC        = dinst->getPC();
  boundaryID        = dinst->getID();
  btb_taken_counter = 0;
  btb_tag_counter   = 0;
}

void BPBTB::fetchBoundaryEnd() {
  btb_taken_counter = -1;
  btb_tag_counter   = 0;
  boundaryPC        = 0;
}

void BPBTB::updateOnly(Dinst* dinst) {
  ++btb_tag_counter;
  if (data.empty() || !dinst->isTaken() || dinst->getInst()->isFuncRet()) {
    return;
  }

  I(btb_taken_counter >= 0);
  btb_taken_counter++;

  bool force_offset = btb_taken_counter > 1 && btb_tag_hybrid;

  auto [boundary_key, tag_key]                    = compute_index_tag(dinst, btb_tag_offset || force_offset, true);
  auto [banked_tag_key, unused_tag_bits, bank_id] = split_tag_bank(tag_key);
  (void)unused_tag_bits;

  BTBCache::CacheLine* cl = data[bank_id]->fillLine(boundary_key, banked_tag_key, 0xdeaddead);
  I(cl);

  cl->targetPC = dinst->getAddr();

#ifdef BTB_TRACE
  fmt::print("update  {} ID={} pc={:x} tag_key={} target={:x} cl:{}\n",
             btb_name,
             dinst->getID(),
             dinst->getPC(),
             tag_key,
             dinst->getAddr(),
             (uint64_t)cl);
#endif
}

std::tuple<Addr_t, Addr_t, size_t> BPBTB::split_tag_bank(Addr_t tag_key) const {
  size_t bank_id = 0;
  if (log2_btb_nsub > 0) {
    bank_id = static_cast<size_t>(tag_key & btb_nsub_mask);
    tag_key = tag_key >> log2_btb_nsub;
  }

  if (tag_key == 0) {
    tag_key = bank_id + 1;
  }

  return {tag_key, tag_key, bank_id};
}

std::tuple<Addr_t, Addr_t> BPBTB::compute_index_tag(Dinst* dinst, bool do_tag_offset, bool doUpdate) {
  uint32_t tag_pc_key = bpred_hash(dinst->getPC());
  uint32_t boundary_key;
  if (btb_fetch_predict) {
    boundary_key = bpred_hash(boundaryPC);
  } else {
    boundary_key = tag_pc_key;
  }

  uint32_t tag_key;
  if (do_tag_offset && btb_fetch_predict) {  // either btb_tag_offset or force_offset
    tag_key = bpred_hash(boundaryPC, dinst->getID() - boundaryID);
    // tag_key = bpred_hash(tag_key, btb_tag_counter);  // NOT btb_taken_counter because it is too easy in pattern
  } else {
    tag_key = bpred_hash(dinst->getPC(), 33);  //
  }
  tag_key &= btb_tag_mask;
  if (tag_key == 0) {  // tag zero, means invalid in cache, so avoid this
    tag_key = btb_tag_counter + 22;
  }

  if (dolc) {
    if (doUpdate) {
      dolc->update(boundaryPC);
    }

    boundary_key ^= dolc->getSign(btbHistorySize, btbHistorySize);
    tag_key ^= dolc->getSign(btbHistorySize, btbHistorySize);
  }

  return {boundary_key, tag_key};  // & tag_mask};
}

Outcome BPBTB::predict(Dinst* dinst, bool doUpdate, bool doStats) {
  // I(dinst->isTaken());  // BTB should be called only when the branch is taken (predict taken & taken -> call BTB)
  ++btb_tag_counter;

  if (dinst->isTransient()) {
    doUpdate = false;
    doStats  = false;
  } else if (!dinst->has_stats()) {
    doStats = false;
  }

  if (data.empty()) {
    // required when BPOracle
    if (dinst->getInst()->doesCtrl2Label()) {
      nHitLabel.inc(doUpdate && doStats);
    } else {
      nHit.inc(doUpdate && doStats);
    }

    return Outcome::Correct;
  }

  if (dinst->getInst()->isFuncRet()) {
    return Outcome::NoBTB;
  }

  I(btb_taken_counter >= 0);
  if (dinst->isTaken()) {
    btb_taken_counter++;
  }

  if (dinst->getInst()->doesCtrl2Label() && btbicache) {
    nHitLabel.inc(doStats && doUpdate);
    return Outcome::Correct;
  }

  bool force_offset                               = btb_taken_counter > 1 && btb_tag_hybrid;
  auto [boundary_key, tag_key]                    = compute_index_tag(dinst, btb_tag_offset || force_offset, doUpdate);
  auto [banked_tag_key, unused_tag_bits, bank_id] = split_tag_bank(tag_key);
  (void)unused_tag_bits;

  BTBCache::CacheLine* cl = nullptr;
  if (doUpdate && dinst->getAddr()) {
    cl = data[bank_id]->fillLine(boundary_key, banked_tag_key, 0xdeaddead);
  } else {
    cl = data[bank_id]->findLineNoEffect(boundary_key, banked_tag_key, 0xdeaddead);
  }

  if (cl) {
    Addr_t predictID = cl->targetPC;
    if (predictID == dinst->getAddr()) {
      nHit.inc(doStats && doUpdate);
      return Outcome::Correct;
    }

    if (doUpdate) {
      cl->targetPC = dinst->getAddr();
    }
    if (predictID) {
#ifdef BTB_TRACE
      fmt::print("BTBMiss1 {} ID={} pc={:x} tag_key={} boundaryPC={:x} update={} predict={:x} target={:x} cl:{}\n",
                 btb_name,
                 dinst->getID(),
                 dinst->getPC(),
                 tag_key,
                 boundaryPC,
                 doUpdate,
                 predictID,
                 dinst->getAddr(),
                 (uint64_t)cl);
#endif
      nMiss.inc(doStats && doUpdate);
      return Outcome::Miss;
    }
#ifdef BTB_TRACE
    fmt::print("Allocat1 {} ID={} pc={:x} tag_key={} update={} predict={:x} target={:x} cl:{}\n",
               btb_name,
               dinst->getID(),
               dinst->getPC(),
               tag_key,
               doUpdate,
               predictID,
               dinst->getAddr(),
               (uint64_t)cl);
#endif
  }
  if (!cl && btb_tag_hybrid) {  // Also try with offset
    std::tie(boundary_key, tag_key)                    = compute_index_tag(dinst, true, doUpdate && btb_taken_counter > 1);
    std::tie(banked_tag_key, unused_tag_bits, bank_id) = split_tag_bank(tag_key);

    if (doUpdate && btb_taken_counter > 1 && dinst->getAddr()) {
      cl = data[bank_id]->fillLine(boundary_key, banked_tag_key, 0xdeaddead);
    } else {
      cl = data[bank_id]->findLineNoEffect(boundary_key, banked_tag_key, 0xdeaddead);
    }

    if (cl) {
      Addr_t predictID = cl->targetPC;
      if (predictID == dinst->getAddr()) {
        nHit.inc(doStats && doUpdate);
        return Outcome::Correct;
      }

      if (doUpdate && btb_taken_counter > 1) {
        cl->targetPC = dinst->getAddr();
      }

      if (predictID) {
#ifdef BTB_TRACE
        fmt::print("BTBMiss2 {} ID={} pc={:x} tag_key={} boundaryPC={:x} update={} predict={:x} target={:x} cl:{}\n",
                   btb_name,
                   dinst->getID(),
                   dinst->getPC(),
                   tag_key,
                   boundaryPC,
                   doUpdate,
                   predictID,
                   dinst->getAddr(),
                   (uint64_t)cl);
#endif
        nMiss.inc(doStats && doUpdate);
        return Outcome::Miss;
      }
#ifdef BTB_TRACE
      fmt::print("Allocat2 {} ID={} pc={:x} tag_key={} update={} predict={:x} target={:x} cl:{}\n",
                 btb_name,
                 dinst->getID(),
                 dinst->getPC(),
                 tag_key,
                 doUpdate,
                 predictID,
                 dinst->getAddr(),
                 (uint64_t)cl);
#endif
    }
  }

  nMiss.inc(doStats && doUpdate);
  return Outcome::NoBTB;
}

/*****************************************
 * BPOracle
 */

Outcome BPOracle::predict(Dinst* dinst, bool doUpdate, bool doStats) {
  if (!dinst->isTaken()) {
    return Outcome::Correct;  // NT
  }

  return btb.predict(dinst, doUpdate, doStats);
}

/*****************************************
 * BPTaken
 */

Outcome BPTaken::predict(Dinst* dinst, bool doUpdate, bool doStats) {
  if (!dinst->getInst()->isBranch() || dinst->isTaken()) {
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

Outcome BPNotTaken::predict(Dinst* dinst, bool doUpdate, bool doStats) {
  if (!dinst->getInst()->isBranch()) {
    return btb.predict(dinst, doUpdate, doStats);
  }

  return dinst->isTaken() ? Outcome::Miss : Outcome::Correct;
}

/*****************************************
 * BPMiss
 */

Outcome BPMiss::predict(Dinst* dinst, bool doUpdate, bool doStats) {
  (void)dinst;
  (void)doUpdate;
  (void)doStats;
  return Outcome::Miss;
}

/*****************************************
 * BPNotTakenEnhaced
 */

Outcome BPNotTakenEnhanced::predict(Dinst* dinst, bool doUpdate, bool doStats) {
  if (!dinst->getInst()->isBranch()) {
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
 * BP2bitL0
 */

BP2bitL0::BP2bitL0(int32_t i, const std::string& section, const std::string& sname)
    : BPred(i, section, sname, "2bitl0")
    , btb(i, section, sname)
    , table(section, Config::get_power2(section, "size", 1), Config::get_integer(section, "bits", 1, 7)) {
  pc                  = 0;
  one_prediction_done = false;
}

void BP2bitL0::fetchBoundaryBegin(Dinst* dinst) {
  BPred::fetchBoundaryBegin(dinst);
  btb.fetchBoundaryBegin(dinst);
  pc                  = dinst->getPC();
  one_prediction_done = false;
}

void BP2bitL0::fetchBoundaryEnd() {
  btb.fetchBoundaryEnd();
  BPred::fetchBoundaryEnd();
  pc = 0;
}

Outcome BP2bitL0::predict(Dinst* dinst, bool doUpdate, bool doStats) {
  // NOTE: 2 bit is simple, no predecode of instruction type (isJump() special code)

  if (one_prediction_done) {
    return Outcome::None;  // Only 1 prediction independent of how many max_bb set
  }
  bool   taken  = dinst->isTaken();
  Addr_t use_pc = pc == 0 ? dinst->getPC() : pc;

  bool ptaken = table.isHighest(calcHist(use_pc));
  if (doUpdate) {
    table.update(calcHist(use_pc), taken);
  }

  Outcome btb_outcome = Outcome::NoBTB;
  if (ptaken) {
    btb_outcome = btb.predict(dinst, doUpdate ? taken : false, doStats);
  }

  if (btb_outcome == Outcome::NoBTB) {
    return Outcome::NoBTB;
  }

  if (ptaken) {
    one_prediction_done = true;
  }
  if (ptaken != taken) {
    return Outcome::Miss;
  }

  return ptaken ? btb_outcome : Outcome::Correct;
}

/*****************************************
 * BP2bit
 */

BP2bit::BP2bit(int32_t i, const std::string& section, const std::string& sname)
    : BPred(i, section, sname, "2bit")
    , btb(i, section, sname)
    , table(section, Config::get_power2(section, "size", 1), Config::get_integer(section, "bits", 1, 7)) {
  pc = 0;
}

void BP2bit::fetchBoundaryBegin(Dinst* dinst) {
  BPred::fetchBoundaryBegin(dinst);
  btb.fetchBoundaryBegin(dinst);
  pc = dinst->getPC();
}

void BP2bit::fetchBoundaryEnd() {
  btb.fetchBoundaryEnd();
  BPred::fetchBoundaryEnd();
  pc = 0;
}

Outcome BP2bit::predict(Dinst* dinst, bool doUpdate, bool doStats) {
  // NOTE: 2 bit is simple, no predecode of instruction type (isJump() special code)

  bool   taken = dinst->isTaken();
  bool   ptaken;
  Addr_t use_pc = pc == 0 ? dinst->getPC() : pc;

  if (doUpdate) {
    ptaken = table.predict(calcHist(use_pc), taken);
  } else {
    ptaken = table.predict(calcHist(use_pc));
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
 * BPTLdbp
 */

BPLdbp::BPLdbp(int32_t i, const std::string& section, const std::string& sname, MemObj* dl1)
    : BPred(i, section, sname, "ldbp"), btb(i, section, sname), DOC_SIZE(Config::get_power2(section, "doc_size")) {
  DL1 = dl1;
}

Outcome BPLdbp::predict(Dinst* dinst, bool doUpdate, bool doStats) {
  if (dinst->getInst()->getOpcode() != Opcode::iBALU_LBRANCH) {  // don't bother about jumps and calls
    return Outcome::None;
  }
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

BPTData::BPTData(int32_t i, const std::string& section, const std::string& sname)
    : BPred(i, section, sname, "tdata")
    , btb(i, section, sname)
    , tDataTable(section, Config::get_power2(section, "size", 1), Config::get_integer(section, "bits", 1, 7)) {}

Outcome BPTData::predict(Dinst* dinst, bool doUpdate, bool doStats) {
  if (!dinst->getInst()->isBranch()) {
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

BPTahead::BPTahead(int32_t i, const std::string& section, const std::string& sname)
    : BPred(i, section, sname, "tahead")
    , btb(i, section, sname)
    , FetchPredict(Config::get_bool(section, "fetch_predict"))
    , btb_fetch_predict(Config::get_bool(section, "btb_fetch_predict")) {
  // int FetchWidth = Config::get_power2("soc", "core", i, "fetch_width", 1);
  // FIXME: I(FetchWidth == TAHEAD_MAXBR);

  tahead = std::make_unique<Tahead>();
}

struct Pending_update {
  Pending_update() {
    other  = false;
    PC     = 0;
    opcode = Opcode::iBALU_LBRANCH;
    taken  = false;
    ptaken = false;
    target = 0;
  }
  Pending_update(bool o, Addr_t pc, Opcode op, bool t, bool pt, Addr_t tgt)
      : other(o), PC(pc), opcode(op), taken(t), ptaken(pt), target(tgt) {}
  bool   other;
  Addr_t PC;
  Opcode opcode;
  bool   taken;
  bool   ptaken;
  Addr_t target;
};

#ifdef TAHEAD_DELAY_UPDATE
std::vector<Pending_update> pending;
#endif

void BPTahead::fetchBoundaryBegin(Dinst* dinst) {
  BPred::fetchBoundaryBegin(dinst);
#ifdef TAHEAD_DELAY_UPDATE
  tahead->fetchBoundaryEnd();
#endif
  btb.fetchBoundaryBegin(dinst);
}

void BPTahead::fetchBoundaryEnd() {
#ifdef TAHEAD_DELAY_UPDATE
  // tahead->fetchBoundaryEnd();

  for (auto& e : pending) {
    tahead->delayed_history(e.PC, e.opcode, e.taken, e.target);
  }
  pending.clear();
#endif
  btb.fetchBoundaryEnd();
  BPred::fetchBoundaryEnd();
}

Outcome BPTahead::predict(Dinst* dinst, bool doUpdate, bool doStats) {
  if (!dinst->getInst()->isBranch()) {
#ifdef TAHEAD_DELAY_UPDATE
    pending.emplace_back(true, dinst->getPC(), dinst->getInst()->getOpcode(), dinst->isTaken(), true, dinst->getAddr());
#endif
    tahead->TrackOtherInst(dinst->getPC(), dinst->getInst()->getOpcode(), dinst->isTaken(), dinst->getAddr());
    dinst->setBiasBranch(true);
    return btb.predict(dinst, doUpdate, doStats);
  }

  bool taken = dinst->isTaken();

  bool   bias   = false;
  Addr_t pc     = dinst->getPC();
  bool   ptaken = tahead->getPrediction(pc, bias);  // pass taken for statistics
  dinst->setBiasBranch(bias);

  if (doUpdate) {
#ifdef TAHEAD_DELAY_UPDATE
    pending.emplace_back(false, pc, dinst->getInst()->getOpcode(), taken, ptaken, dinst->getAddr());
#endif
    tahead->updatePredictor(pc, dinst->getInst()->getOpcode(), taken, ptaken, dinst->getAddr());
  }

  if (taken != ptaken) {
    if (doUpdate) {
      btb.updateOnly(dinst);
    }
    return Outcome::Miss;
  }

  return ptaken ? btb.predict(dinst, doUpdate, doStats) : Outcome::Correct;
}

BPTahead1::BPTahead1(int32_t i, const std::string& section, const std::string& sname)
    : BPred(i, section, sname, "tahead1")
    , btb(i, section, sname)
    , FetchPredict(Config::get_bool(section, "fetch_predict"))
    , btb_fetch_predict(Config::get_bool(section, "btb_fetch_predict")) {
  // int FetchWidth = Config::get_power2("soc", "core", i, "fetch_width", 1);
  // FIXME: I(FetchWidth == TAHEAD1_MAXBR);

  tahead1 = std::make_unique<Tahead1>();
}

/*
struct Pending_update {
  Pending_update () {
    other = false;
    PC = 0;
    opcode = Opcode::iBALU_LBRANCH;
    taken = false;
    ptaken = false;
    target = 0;
  }
  Pending_update(bool o, Addr_t pc, Opcode op, bool t, bool pt, Addr_t tgt)
    : other(o), PC(pc), opcode(op), taken(t), ptaken(pt), target(tgt) {}
  bool other;
  Addr_t PC;
  Opcode opcode;
  bool taken;
  bool ptaken;
  Addr_t target;
};
*/
#ifdef TAHEAD1_DELAY_UPDATE
std::vector<Pending_update> pending;
#endif

void BPTahead1::fetchBoundaryBegin(Dinst* dinst) {
  BPred::fetchBoundaryBegin(dinst);
  (void)dinst;
#ifdef TAHEAD1_DELAY_UPDATE
  tahead1->fetchBoundaryEnd();
#endif
  btb.fetchBoundaryBegin(dinst);
}

void BPTahead1::fetchBoundaryEnd() {
#ifdef TAHEAD1_DELAY_UPDATE
  // tahead1->fetchBoundaryEnd();

  for (auto& e : pending) {
    tahead1->delayed_history(e.PC, e.opcode, e.taken, e.target);
  }
  pending.clear();
#endif
  btb.fetchBoundaryEnd();
  BPred::fetchBoundaryEnd();
}

Outcome BPTahead1::predict(Dinst* dinst, bool doUpdate, bool doStats) {
  if (!dinst->getInst()->isBranch()) {
#ifdef TAHEAD1_DELAY_UPDATE
    pending.emplace_back(true, dinst->getPC(), dinst->getInst()->getOpcode(), dinst->isTaken(), true, dinst->getAddr());
#endif
    tahead1->TrackOtherInst(dinst->getPC(), dinst->getInst()->getOpcode(), dinst->isTaken(), dinst->getAddr());
    dinst->setBiasBranch(true);
    return btb.predict(dinst, doUpdate, doStats);
  }

  bool taken = dinst->isTaken();

  // #define LOWCONF
  bool   bias    = false;
  bool   lowconf = false;
  Addr_t pc      = dinst->getPC();
  bool   ptaken  = tahead1->getPrediction(pc, bias, lowconf);  // pass taken for statistics
  dinst->setBiasBranch(bias);

  if (doUpdate) {
#ifdef TAHEAD1_DELAY_UPDATE
    pending.emplace_back(false, pc, dinst->getInst()->getOpcode(), taken, ptaken, dinst->getAddr());
#endif
    tahead1->updatePredictor(pc, dinst->getInst()->getOpcode(), taken, ptaken, dinst->getAddr());
  }

  if (taken != ptaken) {
    if (doUpdate) {
      btb.updateOnly(dinst);
    }
#ifndef LOWCONF
    return Outcome::Miss;
#else
    return (lowconf && ptaken) ? (Outcome::NoBTB) : Outcome::Miss;
#endif
  }
#ifndef LOWCONF
  return ptaken ? btb.predict(dinst, doUpdate, doStats) : Outcome::Correct;
#else
  return (lowconf && ptaken) ? (Outcome::NoBTB) : (ptaken ? btb.predict(dinst, doUpdate, doStats) : Outcome::Correct);
#endif
}

/*****************************************
 * BPIMLI: SC-TAGE-L with IMLI from Seznec Micro paper
 */

BPIMLI::BPIMLI(int32_t i, const std::string& section, const std::string& sname)
    : BPred(i, section, sname, "imli")
    , btb(i, section, sname)
    , FetchPredict(Config::get_bool(section, "fetch_predict"))
    , btb_fetch_predict(Config::get_bool(section, "btb_fetch_predict"))
    , use_tag_offset(Config::get_bool(section, "use_tag_offset"))
    , use_tag_hybrid(Config::get_bool(section, "use_tag_hybrid")) {
  if (use_tag_offset && use_tag_hybrid) {
    Config::add_error(fmt::format("Section {} has both use_tag_offset and use_tag_hybrid, choose one", sname));
  }

  int FetchWidth = Config::get_power2("soc", "core", i, "fetch_width", 1);

  int bimodal_nsub = Config::has_entry(section, "bimodal_nsub") ? Config::get_power2(section, "bimodal_nsub") : FetchWidth;
  int tage_nsub    = Config::has_entry(section, "tage_nsub") ? Config::get_power2(section, "tage_nsub") : 1;

  int bimodalSize = Config::get_power2(section, "bimodal_size", 4);
  int tageSize    = Config::has_entry(section, "tage_size") ? Config::get_power2(section, "tage_size")
                                                            : ((1 << DEFAULT_LOG2_TAGE_ENTRIES) * tage_nsub);
  int bwidth      = Config::get_integer(section, "bimodal_width");

  int log2_bimodal_nsub    = log2(bimodal_nsub);
  int log2_bimodal_entries = log2(bimodalSize) - log2_bimodal_nsub;
  int log2_tage_entries    = log2(tageSize) - log2(tage_nsub);
  int log2_tage_nsub       = log2(tage_nsub);

  if (tageSize < tage_nsub) {
    Config::add_error(fmt::format("Section {} has tage_size {} smaller than tage_nsub {}", sname, tageSize, tage_nsub));
  }

  int nhist = Config::get_integer(section, "nhist", 1);

  bool statcorrector = Config::get_bool(section, "statcorrector");

  imli = std::make_unique<IMLIBest>(log2_bimodal_nsub,
                                    log2_bimodal_entries,
                                    bwidth,
                                    nhist,
                                    statcorrector,
                                    log2_tage_entries,
                                    log2_tage_nsub);
}

void BPIMLI::fetchBoundaryBegin(Dinst* dinst) {
  if (FetchPredict) {
    boundaryPC = dinst->getPC();
    imli->fetchBoundaryBegin(boundaryPC, dinst->getID());
  }
  if (btb_fetch_predict) {
    btb.fetchBoundaryBegin(dinst);
  }
  taken_counter = 0;
}

void BPIMLI::fetchBoundaryEnd() {
  if (FetchPredict) {
    imli->fetchBoundaryEnd();
  }
  if (btb_fetch_predict) {
    btb.fetchBoundaryEnd();
  }
  taken_counter = -1;
}

Outcome BPIMLI::predict(Dinst* dinst, bool doUpdate, bool doStats) {
  if (!FetchPredict) {
    boundaryPC = dinst->getPC();
    imli->fetchBoundaryBegin(boundaryPC, dinst->getID());
  }
  if (!btb_fetch_predict) {
    btb.fetchBoundaryBegin(dinst);
  }

  I(taken_counter >= 0 && taken_counter < 1024);  // Who does over 1024 taken fetch???

  if (!dinst->getInst()->isBranch()) {
    imli->TrackOtherInst(dinst->getPC(), dinst->getInst()->getOpcode(), dinst->getAddr());
    dinst->setBiasBranch(true);
    if (!FetchPredict) {
      imli->fetchBoundaryEnd();
    }
    return btb.predict(dinst, doUpdate, doStats);
  }

  bool taken = dinst->isTaken();

  // joselimebool bias;
  bool     bias   = false;
  Addr_t   pc     = dinst->getPC();
  uint32_t sign   = 0;
  bool     ptaken = imli->getPrediction(pc,
                                    dinst->getID(),
                                    bias,
                                    sign,
                                    use_tag_offset,
                                    use_tag_hybrid,
                                    taken_counter);  // pass taken for statistics
  dinst->setBiasBranch(bias);
  // printf("BPred.cpp::IMLI::getpredict:: Calculated::ptaken is %b for dinstID %llu at clock cycle %llu\n",
         // ptaken,
         // dinst->getID(),
         // globalClock);

  bool no_alloc = true;
  if (dinst->isUseLevel3()) {
    no_alloc = false;
  }

  if (doUpdate) {
    imli->deferPredictorUpdate(pc,
                               dinst->getID(),
                               taken,
                               ptaken,
                               dinst->getAddr(),
                               no_alloc,
                               use_tag_offset,
                               use_tag_hybrid,
                               taken_counter);
  }

  if (!FetchPredict && !dinst->isTransient()) {
    imli->fetchBoundaryEnd();
  }

  Outcome result;
  if (taken != ptaken) {
    if (doUpdate && !dinst->isTransient()) {
      btb.updateOnly(dinst);
    }
    result = Outcome::Miss;
  } else {
    result = ptaken ? btb.predict(dinst, doUpdate, doStats) : Outcome::Correct;
  }
  // printf("BPred.cpp::IMLI::getpredict::ptaken==taken::for dinstID %llu at clock cycle %llu and ptaken is %b\n",
         // dinst->getID(),
         // globalClock,
         // ptaken);

  if (!btb_fetch_predict) {
    btb.fetchBoundaryEnd();
  }

  return result;
}

// class PREDICTOR;

// gshare_missed, gshare_correct, gshare_incorrect;
BPSuperbp::BPSuperbp(int32_t i, const std::string& section, const std::string& sname)
    : BPred(i, section, sname, "superbp")
    , btb(i, section, sname)
    , FetchPredict(Config::get_bool(section, "fetch_predict"))
    , btb_fetch_predict(Config::get_bool(section, "btb_fetch_predict"))
    , gshare_missed(fmt::format("P({})_{}_BPred:gshare_missed", i, sname))
    , gshare_correct(fmt::format("P({})_{}_BPred:gshare_correct", i, sname))
    , gshare_incorrect(fmt::format("P({})_{}_BPred:gshare_incorrect", i, sname)) {
  // TODO
  /*
  int FetchWidth = Config::get_power2("soc", "core", i, "fetch_width", 1);

  int bimodalSize = Config::get_power2(section, "bimodal_size", 4);
  int bwidth      = Config::get_integer(section, "bimodal_width");
    int blogb          = log2(bimodalSize) - log2(FetchWidth);

  int nhist = Config::get_integer(section, "nhist", 1);

  int log2fetchwidth = log2(FetchWidth);
  */
  int SBP_NUMG = Config::get_integer(section, "SBP_NUMG");
  // int LOG2FETCHWIDTH = Config::get_integer(section, "LOG2FETCHWIDTH");
  int FetchWidth         = Config::get_power2("soc", "core", i, "fetch_width", 1);
  int LOG2FETCHWIDTH     = log2(FetchWidth);
  int NUM_TAKEN_BRANCHES = Config::get_integer(section, "NUM_TAKEN_BRANCHES");

  std::vector<uint32_t> ORIG_ENTRIES_PER_TABLE(SBP_NUMG);
  for (int j = 0; j < SBP_NUMG; j++) {
    ORIG_ENTRIES_PER_TABLE[j] = Config::get_array_integer(section, "ORIG_ENTRIES_PER_TABLE", j);
  }
  /*int ORIG_ENTRIES_PER_TABLE_00 = Config::get_integer(section, "ORIG_ENTRIES_PER_TABLE_00");
  int ORIG_ENTRIES_PER_TABLE_01 = Config::get_integer(section, "ORIG_ENTRIES_PER_TABLE_01");
  int ORIG_ENTRIES_PER_TABLE_02 = Config::get_integer(section, "ORIG_ENTRIES_PER_TABLE_02");
  int ORIG_ENTRIES_PER_TABLE_03 = Config::get_integer(section, "ORIG_ENTRIES_PER_TABLE_03");
  int ORIG_ENTRIES_PER_TABLE_04 = Config::get_integer(section, "ORIG_ENTRIES_PER_TABLE_04");
  int ORIG_ENTRIES_PER_TABLE_05 = Config::get_integer(section, "ORIG_ENTRIES_PER_TABLE_05");
  int ORIG_ENTRIES_PER_TABLE_06 = Config::get_integer(section, "ORIG_ENTRIES_PER_TABLE_06");
  int ORIG_ENTRIES_PER_TABLE_07 = Config::get_integer(section, "ORIG_ENTRIES_PER_TABLE_07");
  int ORIG_ENTRIES_PER_TABLE_08 = Config::get_integer(section, "ORIG_ENTRIES_PER_TABLE_08");
  int ORIG_ENTRIES_PER_TABLE_09 = Config::get_integer(section, "ORIG_ENTRIES_PER_TABLE_09");
  int ORIG_ENTRIES_PER_TABLE_10 = Config::get_integer(section, "ORIG_ENTRIES_PER_TABLE_10");
  int ORIG_ENTRIES_PER_TABLE_11 = Config::get_integer(section, "ORIG_ENTRIES_PER_TABLE_11");  */

  std::vector<uint32_t> INFO_PER_ENTRY(SBP_NUMG);
  for (int j = 0; j < SBP_NUMG; j++) {
    INFO_PER_ENTRY[j] = Config::get_array_integer(section, "INFO_PER_ENTRY", j);
  }
  /*int INFO_PER_ENTRY_00 = Config::get_integer(section, "INFO_PER_ENTRY_00");
  int INFO_PER_ENTRY_01 = Config::get_integer(section, "INFO_PER_ENTRY_01");
  int INFO_PER_ENTRY_02 = Config::get_integer(section, "INFO_PER_ENTRY_02");
  int INFO_PER_ENTRY_03 = Config::get_integer(section, "INFO_PER_ENTRY_03");
  int INFO_PER_ENTRY_04 = Config::get_integer(section, "INFO_PER_ENTRY_04");
  int INFO_PER_ENTRY_05 = Config::get_integer(section, "INFO_PER_ENTRY_05");
  int INFO_PER_ENTRY_06 = Config::get_integer(section, "INFO_PER_ENTRY_06");
  int INFO_PER_ENTRY_07 = Config::get_integer(section, "INFO_PER_ENTRY_07");
  int INFO_PER_ENTRY_08 = Config::get_integer(section, "INFO_PER_ENTRY_08");
  int INFO_PER_ENTRY_09 = Config::get_integer(section, "INFO_PER_ENTRY_09");
  int INFO_PER_ENTRY_10 = Config::get_integer(section, "INFO_PER_ENTRY_10");
  int INFO_PER_ENTRY_11 = Config::get_integer(section, "INFO_PER_ENTRY_11"); */

  uint32_t NUM_GSHARE_ENTRIES_SHIFT = Config::get_integer(section, "NUM_GSHARE_ENTRIES_SHIFT");
  uint8_t  NUM_PAGES_PER_GROUP      = Config::get_integer(section, "NUM_PAGES_PER_GROUP");
  uint8_t  PAGE_OFFSET_SIZE         = Config::get_integer(section, "PAGE_OFFSET_SIZE");
  uint8_t  PAGE_TABLE_INDEX_SIZE    = Config::get_integer(section, "PAGE_TABLE_INDEX_SIZE");

  superbp_p = std::make_unique<PREDICTOR>(SBP_NUMG,
                                          LOG2FETCHWIDTH,
                                          NUM_TAKEN_BRANCHES,
                                          ORIG_ENTRIES_PER_TABLE,
                                          INFO_PER_ENTRY,
                                          NUM_GSHARE_ENTRIES_SHIFT,
                                          NUM_PAGES_PER_GROUP,
                                          PAGE_OFFSET_SIZE,
                                          PAGE_TABLE_INDEX_SIZE);

  // superbp_p = std::make_unique<PREDICTOR>();
}

void BPSuperbp::fetchBoundaryBegin(Dinst* dinst) {
  BPred::fetchBoundaryBegin(dinst);
  if (FetchPredict) {
    superbp_p->fetchBoundaryBegin(dinst->getPC());
  }
  if (btb_fetch_predict) {
    btb.fetchBoundaryBegin(dinst);
  }
}

void BPSuperbp::fetchBoundaryEnd() {
  if (FetchPredict) {
    superbp_p->fetchBoundaryEnd();
  }
  if (btb_fetch_predict) {
    btb.fetchBoundaryEnd();
  }
  BPred::fetchBoundaryEnd();
}

// #define TARGET2_FROM_GSHARE
//  uint64_t gshare_missed, gshare_correct, gshare_incorrect;
Outcome BPSuperbp::predict(Dinst* dinst, bool doUpdate, bool doStats) {
#ifndef TARGET2_FROM_GSHARE
  if (!dinst->getInst()->isBranch()) {
    dinst->setBiasBranch(true);
    return btb.predict(dinst, doUpdate, doStats);
  }
#endif

  uint64_t pc        = dinst->getPC();
  uint8_t  insn_type = dinst->getInst()->isFuncRet()    ? 4 /*insn_t::ret*/
                       : dinst->getInst()->isFuncCall() ? 3 /*insn_t::call*/
                       : dinst->getInst()->isBranch()   ? 2 /*insn_t::branch*/
                       : dinst->getInst()->isJump()     ? 1 /*insn_t::jump*/
                                                        : 0;    /*insn_t::non_cti;*/

  bool     taken        = dinst->isTaken();
  uint64_t branchTarget = dinst->getAddr();

  if (!FetchPredict) {
    superbp_p->fetchBoundaryBegin(dinst->getPC());
  }
  if (!btb_fetch_predict) {
    btb.fetchBoundaryBegin(dinst);
  }
  bool     gshare_use = false, batage_pred = false, batage_conf = false;
  bool     ptaken = false;
  uint64_t gshare_target;
  superbp_p->handle_insn_desesc(pc, branchTarget, insn_type, taken, &batage_pred, &batage_conf, &gshare_use, &gshare_target);
  // xxxx - fmt::print("2.pc={:x} {} {} {} id:{} {} bb:{}\n", dinst->getPC(), gshare_use?"gs":"ng", taken? " T":"NT",
  // batage_pred?" pT":"pNT", dinst->getID(), full_name, dinst->getBB());

#ifdef TARGET2_FROM_GSHARE
  if (!gshare_use) {
    if (!dinst->getInst()->isBranch()) {
      dinst->setBiasBranch(true);
      return btb.predict(dinst, doUpdate, doStats);
    }
  }
#endif

#ifdef TARGET2_FROM_GSHARE
  bool gshare_target_correct = (branchTarget == gshare_target);
#endif

  if (gshare_use) {  // GSHARE did a prediction (it should be taken, but may be a miss prediction)
    switch (last_taken_type) {
      case 1: first_jump.inc(dinst->has_stats()); break;
      case 2: first_br.inc(dinst->has_stats()); break;
      case 3: first_jump.inc(dinst->has_stats()); break;
      case 4: first_ret.inc(dinst->has_stats()); break;
    }
    ptaken = true;
    if ((ptaken != taken)
#ifdef TARGET2_FROM_GSHARE
        || (!gshare_target_correct)
#endif
    ) {
      gshare_incorrect.inc(dinst->has_stats());
    } else {
      gshare_correct.inc(dinst->has_stats());
      // xxxx - fmt::print("gs_correct\n");
      switch (last_taken_type) {
        case 1: first_jump_correct.inc(dinst->has_stats()); break;
        case 2: first_br_correct.inc(dinst->has_stats()); break;
        case 3: first_jump_correct.inc(dinst->has_stats()); break;
        case 4: first_ret_correct.inc(dinst->has_stats()); break;
      }
    }
  } else {
    ptaken = batage_pred;
    if (taken) {
      last_taken_type = insn_type;
      gshare_missed.inc(dinst->has_stats());
    }
  }

  dinst->setBiasBranch(false);
  // TODO: check if it should be true on gshare_use or gshare_correct and if batage_conf = true or batage_pred should be taken as
  // well
  // TODO: Also check that definition of conf b/w desesc and superbp is compatible

#ifdef BATAGE_TEST1
  if (batage_conf || gshare_use) {
    dinst->setBiasBranch(true);
  }
#else
  if (batage_conf && !gshare_use) {
    dinst->setBiasBranch(true);
  }
#endif

  if (!FetchPredict) {
    superbp_p->fetchBoundaryEnd();
  }
  if (!btb_fetch_predict) {
    btb.fetchBoundaryBegin(dinst);
  }

  if (taken != ptaken) {
    if (doUpdate) {
      btb.updateOnly(dinst);
    }
    return Outcome::Miss;
  }

  if (!taken) {
    return Outcome::Correct;
  }

#ifdef TARGET2_FROM_GSHARE
  if (gshare_use) {
    return (gshare_target_correct ? Outcome::Correct : Outcome::Miss);
  }
#endif
  return btb.predict(dinst, doUpdate, doStats);
}

/*****************************************
 * BP2level
 */

BP2level::BP2level(int32_t i, const std::string& section, const std::string& sname)
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

Outcome BP2level::predict(Dinst* dinst, bool doUpdate, bool doStats) {
  if (!dinst->getInst()->isBranch()) {
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

BPHybrid::BPHybrid(int32_t i, const std::string& section, const std::string& sname)
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

Outcome BPHybrid::predict(Dinst* dinst, bool doUpdate, bool doStats) {
  if (!dinst->getInst()->isBranch()) {
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

BP2BcgSkew::BP2BcgSkew(int32_t i, const std::string& section, const std::string& sname)
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

Outcome BP2BcgSkew::predict(Dinst* dinst, bool doUpdate, bool doStats) {
  if (!dinst->getInst()->isBranch()) {
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

BPyags::BPyags(int32_t i, const std::string& section, const std::string& sname)
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

Outcome BPyags::predict(Dinst* dinst, bool doUpdate, bool doStats) {
  if (!dinst->getInst()->isBranch()) {
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

BPOgehl::BPOgehl(int32_t i, const std::string& section, const std::string& sname)
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
  pred = new char*[mtables];
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

Outcome BPOgehl::predict(Dinst* dinst, bool doUpdate, bool doStats) {
  if (!dinst->getInst()->isBranch()) {
    return btb.predict(dinst, doUpdate, doStats);
  }

  bool taken  = dinst->isTaken();
  bool ptaken = false;

  int32_t      S   = 0;  // mtables/2
  HistoryType* iID = (HistoryType*)alloca(mtables * sizeof(HistoryType));

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
  }

  if (taken != ptaken) {
    if (doUpdate) {
      btb.updateOnly(dinst);
    }
    return Outcome::Miss;
  }

  return ptaken ? btb.predict(dinst, doUpdate, doStats) : Outcome::Correct;
}

int32_t BPOgehl::geoidx(uint64_t Add, int64_t* histo, int32_t m, int32_t funct) {
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

  LoopEntry* ent = &table[key % nentries];

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
  const LoopEntry* ent = &table[key % nentries];

  if (ent->tag != tag) {
    return false;
  }

  return (ent->confidence * ent->iterCounter) > 800 && ent->confidence > 7;
}

bool LoopPredictor::isTaken(uint64_t key, uint64_t tag, bool taken) {
  I(isLoop(key, tag));

  LoopEntry* ent = &table[key % nentries];

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
  const LoopEntry* ent = &table[key % nentries];

  if (ent->tag != tag) {
    return 0;
  }

  return ent->iterCounter;
}

/*****************************************
 * BPredictor
 */

std::unique_ptr<BPred> BPredictor::getBPred(int32_t id, const std::string& sec, const std::string& sname, MemObj* DL1) {
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
  } else if (type == "2bitl0") {
    pred = std::make_unique<BP2bitL0>(id, sec, sname);
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
  } else if (type == "tahead") {
    pred = std::make_unique<BPTahead>(id, sec, sname);
  } else if (type == "tahead1") {
    pred = std::make_unique<BPTahead1>(id, sec, sname);
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

BPredictor::BPredictor(int32_t i, MemObj* iobj, MemObj* dobj, std::shared_ptr<BPredictor> bpred)
    : id(i)
    , SMTcopy(bpred != nullptr)
    , il1(iobj)
    , dl1(dobj)
    , nBTAC(fmt::format("P({})_BPred:nBTAC", id))

    , nZero_taken_delay1(fmt::format("P({})_BPred:nZero_taken_delay1", id))
    , nZero_taken_delay2(fmt::format("P({})_BPred:nZero_taken_delay2", id))
    , nZero_taken_delay3(fmt::format("P({})_BPred:nZero_taken_delay3", id))
    , avgTimeBetweenControlMiss(fmt::format("P({})_BPred:avgTimeBetweenControlMiss", id))
    , nControl(fmt::format("P({})_BPred:nControl", id))
    , nBranch(fmt::format("P({})_BPred:nBranch", id))
    , nNoPredict(fmt::format("P({})_BPred:nNoPredict", id))
    , nTaken(fmt::format("P({})_BPred:nTaken", id))
    , nControlMiss(fmt::format("P({})_BPred:nControlMiss", id))
    , nBranchMiss(fmt::format("P({})_BPred:nBranchMiss", id))
    , nBranchBTBMiss(fmt::format("P({})_BPred:nBranchBTBMiss", id))

    , nControl2(fmt::format("P({})_BPred:nControl2", id))
    , nBranch2(fmt::format("P({})_BPred:nBranch2", id))
    , nTaken2(fmt::format("P({})_BPred:nTaken2", id))
    , nControlMiss2(fmt::format("P({})_BPred:nControlMiss2", id))
    , nBranchMiss2(fmt::format("P({})_BPred:nBranchMiss2", id))
    , nBranchBTBMiss2(fmt::format("P({})_BPred:nBranchBTBMiss2", id))

    , nControl3(fmt::format("P({})_BPred:nControl3", id))
    , nBranch3(fmt::format("P({})_BPred:nBranch3", id))
    , nNoPredict3(fmt::format("P({})_BPred:nNoPredict3", id))
    , nHit3_miss2(fmt::format("P({})_BPred:nHit3_miss2", id))
    , nTaken3(fmt::format("P({})_BPred:nTaken3", id))
    , nControlMiss3(fmt::format("P({})_BPred:nControlMiss3", id))
    , nBranchMiss3(fmt::format("P({})_BPred:nBranchMiss3", id))
    , nBranchBTBMiss3(fmt::format("P({})_BPred:nBranchBTBMiss3", id))
    , nFirstBias(fmt::format("P({})_BPred:nFirstBias", id))
    , nFirstBias_wrong(fmt::format("P({})_BPred:nFirstBias_wrong", id))

    , nFixes0(fmt::format("P({})_BPred:nFixes0", id))
    , nFixes1(fmt::format("P({})_BPred:nFixes1", id))
    , nFixes2(fmt::format("P({})_BPred:nFixes2", id))
    , nFixes3(fmt::format("P({})_BPred:nFixes3", id))
    , nUnFixes(fmt::format("P({})_BPred:nUnFixes", id)) {
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

void BPredictor::fetchBoundaryBegin(Dinst* dinst) {
  ras->fetchBoundaryBegin(dinst);
  pred1->fetchBoundaryBegin(dinst);
  if (pred2) {
    pred2->fetchBoundaryBegin(dinst);
    if (pred3) {
      pred3->fetchBoundaryBegin(dinst);
    }
  }
}

void BPredictor::fetchBoundaryEnd() {
  ras->fetchBoundaryEnd();
  pred1->fetchBoundaryEnd();
  if (pred2) {
    pred2->fetchBoundaryEnd();
    if (pred3) {
      pred3->fetchBoundaryEnd();
    }
  }
}

Outcome BPredictor::predict1(Dinst* dinst) {
  // printf("\n\nBPred.cpp::Bpredictor::predict1::Entering predict1::dinstID %llu at clock cycle %llu\n", dinst->getID(), globalClock);
  I(dinst->getInst()->isControl());

  nControl.inc(dinst->has_stats() && !dinst->isTransient());
  nTaken.inc(dinst->isTaken() && dinst->has_stats() && !dinst->isTransient());

  // printf("BPred.cpp::Bpredictor::predict1:: sending pred1->doPredict::dinstID %llu at clock cycle %llu\n",
         // dinst->getID(),
         // globalClock);
  Outcome p = pred1->doPredict(dinst);
  // printf("BPred.cpp::Bpredictor::predict1:: Ending pred1->doPredict::dinstID %llu at clock cycle %llu\n\n",
         // dinst->getID(),
         // globalClock);

  if (dinst->getInst()->isBranch()) {
    nBranch.inc(dinst->has_stats() && !dinst->isTransient());
    nBranchMiss.inc(p == Outcome::Miss && dinst->has_stats() && !dinst->isTransient());
    nBranchBTBMiss.inc(p == Outcome::NoBTB && dinst->has_stats() && !dinst->isTransient());
  }

  nControlMiss.inc((p == Outcome::Miss || p == Outcome::NoBTB) && dinst->has_stats() && !dinst->isTransient());

  nNoPredict.inc(p == Outcome::None && dinst->has_stats() && !dinst->isTransient());

  // printf("BPreictor::predict1::outcome is %d for dinstID %llu at clock cycle %llu\n\n", (int)p, dinst->getID(), globalClock);
  return p;
}

Outcome BPredictor::predict2(Dinst* dinst) {
  // printf("\n\nBPred.cpp::Bpredictor::predict2::Entering predict2::dinstID %llu at clock cycle %llu\n", dinst->getID(), globalClock);
  I(dinst->getInst()->isControl());

  nControl2.inc(dinst->has_stats() && !dinst->isTransient());
  nTaken2.inc(dinst->isTaken() && dinst->has_stats() && !dinst->isTransient());
  // No RAS in L2

  // printf("BPred.cpp::Bpredictor::predict2:: SEnding pred1->doPredict::dinstID %llu at clock cycle %llu\n",
         // dinst->getID(),
         // globalClock);
  Outcome p = pred2->doPredict(dinst);
  // printf("BPred.cpp::Bpredictor::predict2:: Ending pred1->doPredict::dinstID %llu at clock cycle %llu\n",
         // dinst->getID(),
         // globalClock);

  if (dinst->getInst()->isBranch()) {
    nBranch2.inc(dinst->has_stats() && !dinst->isTransient());
    nBranchMiss2.inc(p == Outcome::Miss && dinst->has_stats() && !dinst->isTransient());
    nBranchBTBMiss2.inc(p == Outcome::NoBTB && dinst->has_stats() && !dinst->isTransient());
  }
  nControlMiss2.inc((p == Outcome::Miss || p == Outcome::NoBTB) && dinst->has_stats() && !dinst->isTransient());

  // printf("BPreictor::predict2::outcome is %d for dinstID %llu at clock cycle %llu\n", (int)p, dinst->getID(), globalClock);
  return p;
}

Outcome BPredictor::predict3(Dinst* dinst) {
  // printf("\n\nBPred.cpp::Bpredictor::predict3::Entering predict3::dinstID %llu at clock cycle %llu\n", dinst->getID(), globalClock);
  I(dinst->getInst()->isControl());

  nControl3.inc(dinst->has_stats() && !dinst->isTransient());
  nTaken3.inc(dinst->isTaken() && dinst->has_stats() && !dinst->isTransient());
  // No RAS in L2

  // printf("BPred.cpp::Bpredictor::predict3:: SEnding pred1->doPredict::dinstID %llu at clock cycle %llu\n",
         // dinst->getID(),
         // globalClock);
  Outcome p = pred3->doPredict(dinst);
  // printf("BPred.cpp::Bpredictor::predict3:: Ending pred1->doPredict::dinstID %llu at clock cycle %llu\n",
         // dinst->getID(),
         // globalClock);

  if (dinst->getInst()->isBranch() && !dinst->isTransient()) {
    nBranch3.inc(dinst->has_stats() && !dinst->isTransient());
    nBranchMiss3.inc(p == Outcome::Miss && dinst->has_stats() && !dinst->isTransient());
    nBranchBTBMiss3.inc(p == Outcome::NoBTB && dinst->has_stats() && !dinst->isTransient());
  }
  nControlMiss3.inc((p == Outcome::Miss || p == Outcome::NoBTB) && dinst->has_stats() && !dinst->isTransient());
  nNoPredict3.inc(p == Outcome::None && dinst->has_stats() && !dinst->isTransient());

  // printf("BPreictor::predict3::outcome is %d for dinstID %llu at clock cycle %llu\n", (int)p, dinst->getID(), globalClock);
  return p;
}

// enum class Outcome { Correct(0), None(1), NoBTB(2), Miss(3)}
TimeDelta_t BPredictor::predict(Dinst* dinst, bool* fastfix) {
  *fastfix = true;
  // printf("BPred.cpp::Bpredictor::predict Entering dinstID %llu at clock cycle %llu\n", dinst->getID(), globalClock);

  Outcome outcome1;
  Outcome outcome2 = Outcome::None;
  Outcome outcome3 = Outcome::None;
  dinst->setBiasBranch(false);

  // printf("BPred.cpp::Bpredictor::predict:: sending ras->doPredict::dinstID %llu at clock cycle %llu\n",
         // dinst->getID(),
         // globalClock);
  outcome1 = ras->doPredict(dinst);
  // printf("BPred.cpp::Bpredictor::predict:: complete ras->doPredict::dinstID %llu at clock cycle %llu\n",
         // dinst->getID(),
         // globalClock);
  bool first_bias = false;
  bool last_bias  = false;

  // printf("BPred.cpp::Bpredictor::Entering If nested dinstID %llu at clock cycle %llu\n", dinst->getID(), globalClock);
  if (outcome1 != Outcome::None) {  // If RAS, still call predictors to update history
    // printf("BPred.cpp::Bpredictor::predict:: sending predict1::dinstID %llu at clock cycle %llu\n", dinst->getID(), globalClock);
    predict1(dinst);
    // printf("BPred.cpp::Bpredictor::predict:: complete predict1::dinstID %llu at clock cycle %llu\n", dinst->getID(), globalClock);
    if (pred2) {
      // printf("BPred.cpp::Bpredictor::predict:: sending predict2::dinstID %llu at clock cycle %llu\n", dinst->getID(), globalClock);
      predict2(dinst);
      // printf("BPred.cpp::Bpredictor::predict:: complete predict2::dinstID %llu at clock cycle %llu\n", dinst->getID(), globalClock);
    }
    if (pred3) {
      // printf("BPred.cpp::Bpredictor::predict:: sending predict3::dinstID %llu at clock cycle %llu\n", dinst->getID(), globalClock);
      predict3(dinst);
      // printf("BPred.cpp::Bpredictor::predict:: complete predict3::dinstID %llu at clock cycle %llu\n", dinst->getID(), globalClock);
    }
    outcome2 = outcome1;
    outcome3 = outcome1;
  } else {
    // printf("BPred.cpp::Bpredictor::predict::ELSE::OUTCOME==None:: sending predict1::dinstID %llu at clock cycle %llu\n",
           // dinst->getID(),
           // globalClock);
    outcome1 = predict1(dinst);
    // printf(
        // "BPred.cpp::Bpredictor::predict::ELSE::OUTCOME==None:: ending predict1::outcome1 Is %d for dinstID %llu at clock cycle "
        // "%llu\n",
        // (int)outcome1,
        // dinst->getID(),
        // globalClock);
    if (pred2) {
      if (!pred3 && !dinst->isTransient()) {
        first_bias = dinst->isBiasBranch();
      }
      // printf("BPred.cpp::Bpredictor::predict::ELSE::OUTCOME==None:: ending predict2::dinstID %llu at clock cycle %llu\n",
             // dinst->getID(),
             // globalClock);
      outcome2 = predict2(dinst);
      // printf(
          // "BPred.cpp::Bpredictor::predict::ELSE::OUTCOME==None:: ending predict2::outcome2 Is %d for dinstID %llu at clock cycle "
          // "%llu\n",
          // (int)outcome2,
          // dinst->getID(),
          // globalClock);
    } else {
      outcome2 = outcome1;
      // printf(
          // "BPred.cpp::Bpredictor::predict::ELSE::OUTCOME==None:: No pred2::outcome2 Is outcome1 is %d for dinstID %llu at clock "
          // "cycle %llu\n",
          // (int)outcome2,
          // dinst->getID(),
          // globalClock);
    }
    if (pred3) {
      first_bias = dinst->isBiasBranch();
      // printf("BPred.cpp::Bpredictor::predict::ELSE::OUTCOME==None:: ending predict3::dinstID %llu at clock cycle %llu\n",
             // dinst->getID(),
             // globalClock);
      outcome3 = predict3(dinst);
      // printf("BPred.cpp::Bpredictor::predict::ELSE::OUTCOME==None:: ending predict3::dinstID %llu at clock cycle %llu\n",
             // dinst->getID(),
             // globalClock);
    } else {
      outcome3 = outcome2;
    }
    if (!dinst->isTransient()) {
      last_bias = dinst->isBiasBranch();
    }
  }

  if (dinst->getInst()->isFuncRet() || dinst->getInst()->isFuncCall()) {
    dinst->setBiasBranch(true);
    if (!dinst->isTransient()) {
      ras->tryPrefetch(il1, dinst->has_stats(), 1);
    }
  }

  if (first_bias && !last_bias) {
    nFirstBias.inc(dinst->has_stats() && !dinst->isTransient());
    if (pred3) {  // Bias from pred2 to pred3
      if (outcome2 != Outcome::Correct && outcome3 == Outcome::Correct) {
        nFirstBias_wrong.inc(dinst->has_stats() && !dinst->isTransient());
      }
      outcome3 = outcome2;
    } else {  // Bias from pred1 to pred2 (pred3 does not exit)
      if (outcome1 != Outcome::Correct && outcome3 == Outcome::Correct) {
        nFirstBias_wrong.inc(dinst->has_stats() && !dinst->isTransient());
      }
      outcome2 = outcome1;
      outcome3 = outcome1;
    }
  }

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
      if (!dinst->isTransient()) {
        nHit3_miss2.inc(true);
      }
    }
  }

  if (dinst->isTaken()) {
    // xxxx - fmt::print("3.pc={:x} l0:{} l1:{} l2:{} BB:{} @{} :", dinst->getPC(), BPred::to_s(outcome1), BPred::to_s(outcome2),
    // BPred::to_s(outcome3), dinst->getBB(), globalClock);
    if (outcome1 == Outcome::Correct && outcome2 == Outcome::Correct && outcome3 == Outcome::Correct) {
      nFixes1.inc(dinst->has_stats() && !dinst->isTransient());  // Can get lucky and BB=1 predict too but weird
      // xxxx - fmt::print(" a 1\n");
      // printf("BPreictor::predict::isTaken()::bpredDelay1 is %d for dinstID %llu at clock cycle %llu\n",
             // bpredDelay1,
             // dinst->getID(),
             // globalClock);
      return bpredDelay1;
    }
    // }else{
    //   // Still allowed to fetch more BB
    //   if (outcome1 != Outcome::Miss && outcome2 == Outcome::Correct && outcome3 == Outcome::Correct) {
    //     nFixes0.inc(dinst->has_stats());
    //     // xxxx - fmt::print(" b 0\n");
    //     return 0;
    //   }
    // }
  } else {
    // xxxx - fmt::print("4.pc={:x} l0:{} l1:{} l2:{} BB:{} @{} :", dinst->getPC(), BPred::to_s(outcome1), BPred::to_s(outcome2),
    // BPred::to_s(outcome3), dinst->getBB(), globalClock);
    //  Not Taken can join FetchBlock if no miss predicts

    // Any mix of NoBTB or Correct will do it for not-taken
    if (outcome1 != Outcome::Miss && outcome2 != Outcome::Miss && outcome3 != Outcome::Miss) {
      nFixes0.inc(dinst->has_stats() && !dinst->isTransient());
      // xxxx - fmt::print(" c 0\n");
      // printf("BPreictor::predict::isNOTTaken()::bpredDelay is 0 for dinstID %llu at clock cycle %llu\n",
             // dinst->getID(),
             // globalClock);
      return 0;
    }
  }

  TimeDelta_t bpred_total_delay = 0;

  if (outcome2 == Outcome::Correct && outcome3 == Outcome::Correct) {
    nFixes2.inc(dinst->has_stats() && !dinst->isTransient());
    // printf("BPreictor::predict::bpredDelay2 is %d for dinstID %llu at clock cycle %llu\n",
           // bpredDelay2,
           // dinst->getID(),
           // globalClock);
    bpred_total_delay = bpredDelay2;
    // xxxx - fmt::print(" d 2\n");
  } else if (outcome3 == Outcome::Correct) {
    nFixes3.inc(dinst->has_stats() && !dinst->isTransient());
    // printf("BPreictor::predict::bpredDelay3 is %d for dinstID %llu at clock cycle %llu\n",
           // bpredDelay3,
           // dinst->getID(),
           // globalClock);
    bpred_total_delay = bpredDelay3;
    // xxxx - fmt::print(" d 3\n");
  } else {
    nUnFixes.inc(dinst->has_stats() && !dinst->isTransient());
    *fastfix          = false;
    bpred_total_delay = 1;  // Anything but zero
    // xxxx - fmt::print(" d miss\n");
  }

  // fmt::print("4.pc={:x} l0:{} l1:{} l2:{} BB:{} @{} delta={}\n", dinst->getPC(), BPred::to_s(outcome1), BPred::to_s(outcome2),
  // BPred::to_s(outcome3), dinst->getBB(), globalClock, globalClock-lastControlMiss);
  avgTimeBetweenControlMiss.sample(globalClock - lastControlMiss, dinst->has_stats());
  lastControlMiss = globalClock;

  // printf("BPreictor::predict::TotalbpredDelay is %d for dinstID %llu at clock cycle %llu\n",
         // bpred_total_delay,
         // dinst->getID(),
         // globalClock);
  return bpred_total_delay;
}

void BPredictor::dump(const std::string& str) const {
  (void)str;
  // nothing?
}
