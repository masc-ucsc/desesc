// See LICENSE for details.

#pragma once

/*
 * The original Branch predictor code was inspired in simplescalar-3.0, but
 * heavily modified to make it more OOO. Now, it supports more branch predictors
 * that the standard simplescalar distribution.
 *
 * Supported branch predictors models:
 *
 * Oracle, NotTaken, Taken, 2bit, 2Level, 2BCgSkew
 *
 */

#include <algorithm>
#include <deque>
#include <iostream>
#include <map>
#include <vector>

#include "cachecore.hpp"
#include "dinst.hpp"
#include "dolc.hpp"
#include "estl.hpp"
#include "iassert.hpp"
#include "sctable.hpp"
#include "stats.hpp"

#define RAP_T_NT_ONLY 1
// #define DOC_SIZE 512 //128
enum BrOpType { BEQ = 0, BNE = 1, BLT = 4, BGE = 5, BLTU = 6, BGEU = 7, ILLEGAL_BR = 8 };
enum class Outcome { Correct, None, NoBTB, Miss };

class MemObj;

class BPred {
public:
  typedef int64_t HistoryType;
  class Hash4HistoryType {
  public:
    size_t operator()(const HistoryType &addr) const { return ((addr) ^ (addr >> 16)); }
  };

  HistoryType calcHist(Addr_t pc) const {
    HistoryType cid = pc >> 1;  // psudo-PC works, no need lower 1 bit in RISC-V

    // Remove used bits (restrict predictions per cycle)
    cid = cid >> addrShift;
    // randomize it
    cid = (cid >> 17) ^ (cid);

    return cid;
  }

protected:
  const int32_t id;

  Stats_cntr nHit;   // N.B. predictors should not update these counters directly
  Stats_cntr nMiss;  // in their predict() function.

  int32_t addrShift;
  int32_t maxCores;

public:
  BPred(int32_t i, const std::string &section, const std::string &sname, const std::string &name);
  virtual ~BPred();

  virtual Outcome predict(Dinst *dinst, bool doUpdate, bool doStats) = 0;
  virtual void    fetchBoundaryBegin(Dinst *dinst);  // If the branch predictor support fetch boundary model, do it
  virtual void    fetchBoundaryEnd();                // If the branch predictor support fetch boundary model, do it

  Outcome doPredict(Dinst *dinst, bool doStats = true) {
    Outcome pred = predict(dinst, true, doStats);
    if (pred == Outcome::None) {
      return pred;
    }

    if (dinst->getInst()->isJump()) {
      return pred;
    }

    nHit.inc(pred == Outcome::Correct && dinst->has_stats() && doStats);
    nMiss.inc(pred == Outcome::Miss && dinst->has_stats() && doStats);

    return pred;
  }

  void update(Dinst *dinst) { predict(dinst, true, false); }
};

class BPRas : public BPred {
private:
  const uint16_t RasSize;
  const uint16_t rasPrefetch;

  std::vector<Addr_t> stack;
  int32_t             index;

protected:
public:
  BPRas(int32_t i, const std::string &section, const std::string &sname);
  ~BPRas();
  Outcome predict(Dinst *dinst, bool doUpdate, bool doStats);

  void tryPrefetch(MemObj *il1, bool doStats, int degree);
};

class BPBTB {
private:
  Stats_cntr nHit;
  Stats_cntr nMiss;
  Stats_cntr nHitLabel;  // hits to the icache label (ibtb)
  DOLC      *dolc;
  bool       btbicache;
  uint64_t   btbHistorySize;

  class BTBState : public StateGeneric<Addr_t> {
  public:
    BTBState(int32_t lineSize) {
      (void)lineSize;
      inst = 0;
    }

    Addr_t inst;

    bool operator==(BTBState s) const { return inst == s.inst; }
  };

  typedef CacheGeneric<BTBState, Addr_t> BTBCache;

  BTBCache *data;

protected:
public:
  BPBTB(int32_t i, const std::string &section, const std::string &sname, const std::string &name = "btb");
  ~BPBTB();

  Outcome predict(Addr_t pc, Dinst *dinst, bool doUpdate, bool doStats);
  void    updateOnly(Addr_t pc, Dinst *dinst);
};

class BPOracle : public BPred {
private:
  BPBTB btb;

protected:
public:
  BPOracle(int32_t i, const std::string &section, const std::string &sname)
      : BPred(i, section, sname, "Oracle"), btb(i, section, sname) {}

  Outcome predict(Dinst *dinst, bool doUpdate, bool doStats);
};

class BPNotTaken : public BPred {
private:
  BPBTB btb;

protected:
public:
  BPNotTaken(int32_t i, const std::string &section, const std::string &sname)
      : BPred(i, section, sname, "NotTaken"), btb(i, section, sname) {
    // Done
  }

  Outcome predict(Dinst *dinst, bool doUpdate, bool doStats);
};

class BPMiss : public BPred {
private:
protected:
public:
  BPMiss(int32_t i, const std::string &section, const std::string &sname) : BPred(i, section, sname, "Miss") {
    // Done
  }

  Outcome predict(Dinst *dinst, bool doUpdate, bool doStats);
};

class BPNotTakenEnhanced : public BPred {
private:
  BPBTB btb;

protected:
public:
  BPNotTakenEnhanced(int32_t i, const std::string &section, const std::string &sname)
      : BPred(i, section, sname, "NotTakenEnhanced"), btb(i, section, sname) {
    // Done
  }

  Outcome predict(Dinst *dinst, bool doUpdate, bool doStats);
};

class BPTaken : public BPred {
private:
  BPBTB btb;

protected:
public:
  BPTaken(int32_t i, const std::string &section, const std::string &sname)
      : BPred(i, section, sname, "Taken"), btb(i, section, sname) {
    // Done
  }

  Outcome predict(Dinst *dinst, bool doUpdate, bool doStats);
};

class BP2bit : public BPred {
private:
  BPBTB btb;

  SCTable table;

  Addr_t pc;

protected:
public:
  BP2bit(int32_t i, const std::string &section, const std::string &sname);

  void    fetchBoundaryBegin(Dinst *dinst);
  void    fetchBoundaryEnd();
  Outcome predict(Dinst *dinst, bool doUpdate, bool doStats);
};

class IMLIBest;

class BPIMLI : public BPred {
private:
  BPBTB btb;

  std::unique_ptr<IMLIBest> imli;

  const bool FetchPredict;

protected:
public:
  BPIMLI(int32_t i, const std::string &section, const std::string &sname);

  void    fetchBoundaryBegin(Dinst *dinst);
  void    fetchBoundaryEnd();
  Outcome predict(Dinst *dinst, bool doUpdate, bool doStats);
};

// FIXME: convert to just class Tahead;
class Tahead;
/*
class Tahead {
public:
  bool getPrediction(uint64_t PCBRANCH) {
    (void)PCBRANCH;
    return false;
  }
};
*/
class BPTahead : public BPred {
private:
  BPBTB btb;

  std::unique_ptr<Tahead> tahead;

  const bool FetchPredict;

protected:
public:
  BPTahead(int32_t i, const std::string &section, const std::string &sname);

  void    fetchBoundaryBegin(Dinst *dinst);
  void    fetchBoundaryEnd();
  Outcome predict(Dinst *dinst, bool doUpdate, bool doStats);
};

// FIXME: convert to just class Tahead;
class Tahead1;
/*
class Tahead1 {
public:
  bool getPrediction(uint64_t PCBRANCH) {
    (void)PCBRANCH;
    return false;
  }
};
*/
class BPTahead1 : public BPred {
private:
  BPBTB btb;

  std::unique_ptr<Tahead1> tahead1;

  const bool FetchPredict;

protected:
public:
  BPTahead1(int32_t i, const std::string &section, const std::string &sname);

  void    fetchBoundaryBegin(Dinst *dinst);
  void    fetchBoundaryEnd();
  Outcome predict(Dinst *dinst, bool doUpdate, bool doStats);
};



// class PREDICTOR;
#include "predictor.hpp"
class BPSuperbp : public BPred {
private:
  BPBTB btb;

  std::unique_ptr<PREDICTOR> superbp_p;
  const bool                 FetchPredict;
  Stats_cntr                 gshare_must;
  Stats_cntr                 gshare_correct;
  Stats_cntr                 gshare_incorrect;

protected:
public:
  BPSuperbp(int32_t i, const std::string &section, const std::string &sname);

  void    fetchBoundaryBegin(Dinst *dinst);
  void    fetchBoundaryEnd();
  Outcome predict(Dinst *dinst, bool doUpdate, bool doStats);
};

class BP2level : public BPred {
private:
  BPBTB btb;

  const uint16_t l1Size;
  const uint32_t l1SizeMask;

  const uint16_t    historySize;
  const HistoryType historyMask;

  SCTable globalTable;

  DOLC         dolc;
  HistoryType *historyTable;  // LHR
  bool         useDolc;

protected:
public:
  BP2level(int32_t i, const std::string &section, const std::string &sname);
  ~BP2level();

  Outcome predict(Dinst *dinst, bool doUpdate, bool doStats);
};

class BPHybrid : public BPred {
private:
  BPBTB btb;

  const uint16_t    historySize;
  const HistoryType historyMask;

  SCTable globalTable;

  HistoryType ghr;  // Global History Register

  SCTable localTable;
  SCTable metaTable;

protected:
public:
  BPHybrid(int32_t i, const std::string &section, const std::string &sname);
  ~BPHybrid();

  Outcome predict(Dinst *dinst, bool doUpdate, bool doStats);
};

class BP2BcgSkew : public BPred {
private:
  BPBTB btb;

  SCTable BIM;

  SCTable           G0;
  const uint16_t    G0HistorySize;
  const HistoryType G0HistoryMask;

  SCTable           G1;
  const uint16_t    G1HistorySize;
  const HistoryType G1HistoryMask;

  SCTable           metaTable;
  const uint16_t    MetaHistorySize;
  const HistoryType MetaHistoryMask;

  HistoryType history;

protected:
public:
  BP2BcgSkew(int32_t i, const std::string &section, const std::string &sname);
  ~BP2BcgSkew();

  Outcome predict(Dinst *dinst, bool doUpdate, bool doStats);
};

class BPyags : public BPred {
private:
  BPBTB btb;

  const uint16_t    historySize;
  const HistoryType historyMask;

  SCTable table;
  SCTable ctableTaken;
  SCTable ctableNotTaken;

  HistoryType ghr;  // Global History Register

  uint8_t    *CacheTaken;
  HistoryType CacheTakenMask;
  HistoryType CacheTakenTagMask;

  uint8_t    *CacheNotTaken;
  HistoryType CacheNotTakenMask;
  HistoryType CacheNotTakenTagMask;

protected:
public:
  BPyags(int32_t i, const std::string &section, const std::string &sname);
  ~BPyags();

  Outcome predict(Dinst *dinst, bool doUpdate, bool doStats);
};

class BPOgehl : public BPred {
private:
  BPBTB btb;

  const int32_t mtables;
  const int32_t max_history_size;
  const int32_t nentry;
  const int32_t addwidth;
  int32_t       logpred;

  int32_t THETA;
  int32_t MAXTHETA;
  int32_t THETAUP;
  int32_t PREDUP;

  int64_t *ghist;
  int32_t *histLength;
  int32_t *usedHistLength;

  int32_t *T;
  int32_t  AC;
  uint8_t  miniTag;
  uint8_t *MINITAG;

  uint8_t genMiniTag(const Dinst *dinst) const {
    Addr_t t = dinst->getPC() >> 2;
    return (uint8_t)((t ^ (t >> 3)) & 3);
  }

  char  **pred;
  int32_t TC;

protected:
  int32_t geoidx(uint64_t Add, int64_t *histo, int32_t m, int32_t funct);

public:
  BPOgehl(int32_t i, const std::string &section, const std::string &sname);
  ~BPOgehl();

  Outcome predict(Dinst *dinst, bool doUpdate, bool doStats);
};

class LoopPredictor {
private:
  struct LoopEntry {
    uint64_t tag;
    uint32_t iterCounter;
    uint32_t currCounter;
    int32_t  confidence;
    uint8_t  dir;

    LoopEntry() {
      tag         = 0;
      confidence  = 0;
      iterCounter = 0;
      currCounter = 0;
    }
  };

  const uint32_t nentries;
  LoopEntry     *table;  // TODO: add assoc, now just BIG

public:
  LoopPredictor(int size);
  void update(uint64_t key, uint64_t tag, bool taken);

  bool     isLoop(uint64_t key, uint64_t tag) const;
  bool     isTaken(uint64_t key, uint64_t tag, bool taken);
  uint32_t getLoopIter(uint64_t key, uint64_t tag) const;
};

class BPTData : public BPred {
private:
  BPBTB btb;

  SCTable tDataTable;

  struct tDataTableEntry {
    tDataTableEntry() {
      tag = 0;
      ctr = 0;
    }

    Addr_t tag;
    int8_t ctr;
  };

  HASH_MAP<Addr_t, tDataTableEntry> tTable;

protected:
public:
  BPTData(int32_t i, const std::string &section, const std::string &sname);
  ~BPTData() {}

  Outcome predict(Dinst *dinst, bool doUpdate, bool doStats);
};

/*LOAD BRANCH PREDICTOR (LDBP)*/
class BPLdbp : public BPred {
private:
  BPBTB btb;
#if 0
    SCTable ldbp_table;
    std::map<uint64_t, bool> ldbp_map;

    struct ldbp_table{
      ldbp_table(){
        tag = 0;
        ctr = 0;
      }

      Addr_t tag;
      int8_t ctr;
    };
#endif

public:
  const int DOC_SIZE;
  MemObj   *DL1;

  BPLdbp(int32_t i, const std::string &section, const std::string &sname, MemObj *dl1 = 0);
  ~BPLdbp() {}

  Outcome  predict(Dinst *dinst, bool doUpdate, bool doStats);
  bool     outcome_calculator(BrOpType br_op, Data_t br_data1, Data_t br_data2);
  BrOpType branch_type(Addr_t brpc);

  struct data_outcome_correlator {
    data_outcome_correlator() {
      tag    = 0;
      taken  = 0;
      ntaken = 0;
    }
    Addr_t tag;
    int    taken;   // taken counter
    int    ntaken;  // not taken counter

    int update_doc(Addr_t _tag, bool doc_miss, bool outcome) {
      // extract current outcome here
      int max_counter = 7;
      int conf_pred   = 0;

      if (!doc_miss) {
        tag       = _tag;
        conf_pred = doc_compute();
      } else {
        taken  = 0;
        ntaken = 0;
      }
      // this if loop updates DOC counters
      if (outcome == true) {
        if (taken < max_counter) {
          taken++;
        } else if (ntaken > 0) {
          ntaken--;
        }
      } else {
        if (ntaken < max_counter) {
          ntaken++;
        } else if (taken > 0) {
          taken--;
        }
      }
      return conf_pred;
    }

    int doc_compute() {
      int med = (taken == (2 * ntaken + 1)) || (ntaken == (2 * taken + 1));
      int low = (taken < (2 * ntaken + 1)) && (ntaken < (2 * taken + 1));
      int m   = 2 * low + med;
#if 0
        if(taken == 7) {
          return 1;
        }else if(ntaken == 7) {
          return 2;
        }else {
          return 0;
        }
#endif
      if (m < 1) {
        if (taken > ntaken) {
          return 1;
        }
        return 2;
      }
      return 0;
    }
  };

  std::vector<data_outcome_correlator> doc_table = std::vector<data_outcome_correlator>(DOC_SIZE);
  // std::vector<data_outcome_correlator> doc_table;

  int outcome_doc(Dinst *dinst, Addr_t _tag, bool outcome) {
    (void)dinst;
    // tag is 2n bits
    Addr_t t     = (_tag >> (int)log2i(DOC_SIZE)) & (DOC_SIZE - 1);  // upper n bits for tag
    int    index = _tag & (DOC_SIZE - 1);                            // lower n bits for index
    if (doc_table[index].tag == t) {
      return doc_table[index].update_doc(t, false, outcome);
    }

    // DOC miss
    doc_table[index].tag = t;
    return doc_table[index].update_doc(t, true, outcome);
  }
};

class BPredictor {
private:
  const int32_t id;
  const bool    SMTcopy;
  MemObj       *il1;  // For prefetch
  MemObj       *dl1;  //

  std::unique_ptr<BPRas> ras;
  std::shared_ptr<BPred> pred1;
  std::shared_ptr<BPred> pred2;
  std::shared_ptr<BPred> pred3;

  int32_t FetchWidth;
  int32_t bpredDelay1;
  int32_t bpredDelay2;
  int32_t bpredDelay3;

  Stats_cntr nBTAC;

  Stats_cntr nZero_taken_delay;

  Stats_cntr nControl;
  Stats_cntr nBranch;
  Stats_cntr nNoPredict;
  Stats_cntr nTaken;
  Stats_cntr nControlMiss;
  Stats_cntr nBranchMiss;     // Miss predict due to T/NT predictor = nBranchMiss / nBranch
  Stats_cntr nBranchBTBMiss;  // Extra miss due to additional BTB mistake

  Stats_cntr nControl2;
  Stats_cntr nBranch2;
  Stats_cntr nTaken2;
  Stats_cntr nControlMiss2;
  Stats_cntr nBranchMiss2;
  Stats_cntr nBranchBTBMiss2;

  Stats_cntr nControl3;
  Stats_cntr nBranch3;
  Stats_cntr nNoPredict3;
  Stats_cntr nHit3_miss2;  // Mispred of Level 2 which are fixed by level 3 BPred
  Stats_cntr nTaken3;
  Stats_cntr nControlMiss3;
  Stats_cntr nBranchMiss3;
  Stats_cntr nBranchBTBMiss3;
  Stats_cntr nFirstBias;
  Stats_cntr nFirstBias_wrong;

  Stats_cntr nFixes1;
  Stats_cntr nFixes2;
  Stats_cntr nFixes3;
  Stats_cntr nUnFixes;

protected:
  Outcome predict1(Dinst *dinst);
  Outcome predict2(Dinst *dinst);
  Outcome predict3(Dinst *dinst);

public:
  BPredictor(int32_t i, MemObj *il1, MemObj *DL1, std::shared_ptr<BPredictor> bpred = nullptr);

  static std::unique_ptr<BPred> getBPred(int32_t id, const std::string &sname, const std::string &sec, MemObj *dl1 = 0);

  void        fetchBoundaryBegin(Dinst *dinst);
  void        fetchBoundaryEnd();
  TimeDelta_t predict(Dinst *dinst, bool *fastfix);
  bool        Miss_Prediction(Dinst *dinst);
  void        dump(const std::string &str) const;
};
