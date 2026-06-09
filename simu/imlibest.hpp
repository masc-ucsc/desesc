/*Copyright (c) <2006>, INRIA : Institut National de Recherche en Informatique et en Automatique (French National Research Institute
 for Computer Science and Applied Mathematics) All rights reserved.

 Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following
 conditions are met:

 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
 in the documentation and/or other materials provided with the distribution.

 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived
 from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT
 NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// #define LARGE_SC
// #define STRICTSIZE
//  uncomment to get the 256 Kbits record predictor mentioned in the paper achieves 2.228 MPKI

// #define POSTPREDICT
//  uncomment to get a realistic predictor around 256 Kbits , with 12 1024 entries tagged tables in the TAGE predictor, and a global
//  history and single local history GEHL statistical corrector total misprediction numbers TAGE-SC-L : 2.435 MPKI TAGE-SC-L  +
//  IMLI: 2.294 MPKI TAGE-GSC + IMLI: 2.370 MPKI TAGE-GSC : 2.531 MPKI TAGE alone: 2.602 MPKI

#pragma once

#include <inttypes.h>
#include <math.h>

#include <print>
#include <vector>
// Rotate/XOR cascade mixer (no multiplies). Produces 64-bit mixed value.
[[nodiscard]] constexpr std::uint64_t imli_bpred_hash(Addr_t x) noexcept {
  // Rotate-heavy avalanche; all ops are shifts/xors/ors.
  x ^= std::rotl(x, 5);
  x ^= std::rotl(x, 13);
  x ^= (x >> 32);
  x ^= std::rotl(x, 7);
  x ^= (x >> 23);
  x ^= std::rotl(x, 17);
  x ^= (x >> 29);
  x ^= std::rotl(x, 41);
  return x;
}

[[nodiscard]] constexpr std::uint64_t imli_bpred_hash(Addr_t addr, Addr_t offset) noexcept {
  // Pre-mix both inputs so the later combines aren't "linear-ish".
  std::uint64_t a = imli_bpred_hash(addr);
  std::uint64_t c = imli_bpred_hash(offset ^ std::rotl(offset, 19) ^ (offset << 1));

  a ^= std::rotl(c, 9);
  a ^= std::rotl(c, 27);
  a ^= (c >> 17);

  a ^= std::rotl(a ^ c, 33);

  return imli_bpred_hash(a);
}

#include "dinst.hpp"  // Addr_t and Opcode
#include "dolc.hpp"

#define MEDIUM_TAGE 1
// #define IMLI_150K 1
// #define IMLI_256K 1
// #define MEGA_IMLI 1

#if defined(MEGA_IMLI) && defined(MEDIUM_TAGE)
#error "Pick one"
#endif

#define SIMPLER_DOLC_PATH

#ifdef MEDIUM_TAGE
// #define LOOPPREDICTOR //  use loop  predictor
// #define LOCALH			// use local histories
// #define IMLI			// using IMLI component
// #define IMLISIC            //use IMLI-SIC
// #define IMLIOH		//use IMLI-OH
#define IMLI                         // using IMLI component
#define DEFAULT_LOG2_TAGE_ENTRIES 7  /* logsize of the tagged TAGE tables*/
#define TBITS                     13 /* minimum tag width*/
#define MAXHIST                   300
#define MINHIST                   5
// #define USE_DOLC 1

#elif MEGA_IMLI                       // 1M IMLI
// nhist = 9
#define LOOPPREDICTOR                 //  use loop  predictor
#define LOCALH                        // use local histories
#define IMLI                          // using IMLI component
#define IMLISIC                       // use IMLI-SIC
#define IMLIOH                        // use IMLI-OH
#define DEFAULT_LOG2_TAGE_ENTRIES 12  // logsize of the tagged TAGE tables
#define TBITS                     22  // minimum tag width
#define MAXHIST                   400
#define MINHIST                   5

#elif IMLI_256K
// nhist = 6
#define LOOPPREDICTOR                 //  use loop  predictor
#define LOCALH                        // use local histories
#define IMLI                          // using IMLI component
#define IMLISIC                       // use IMLI-SIC
#define IMLIOH                        // use IMLI-OH
#define DEFAULT_LOG2_TAGE_ENTRIES 11  // logsize of the tagged TAGE tables
#define TBITS                     16  // minimum tag width
#define MAXHIST                   200
#define MINHIST                   5

#elif IMLI_256K_SBP
// nhist = 6
#define LOOPPREDICTOR                 //  use loop  predictor
#define LOCALH                        // use local histories
#define IMLI                          // using IMLI component
#define IMLISIC                       // use IMLI-SIC
#define IMLIOH                        // use IMLI-OH
#define DEFAULT_LOG2_TAGE_ENTRIES 10  // logsize of the tagged TAGE tables
#define TBITS                     12  // minimum tag width
#define MAXHIST                   700
#define MINHIST                   4
#elif IMLI_150K
// nhist = 4
#define LOOPPREDICTOR                 //  use loop  predictor
#define LOCALH                        // use local histories
#define IMLI                          // using IMLI component
#define IMLISIC                       // use IMLI-SIC
#define IMLIOH                        // use IMLI-OH
#define DEFAULT_LOG2_TAGE_ENTRIES 11  // 11       // logsize of the tagged TAGE tables
#define TBITS                     13  // 16      // minimum tag width
#define MAXHIST                   160
#define MINHIST                   5
#else
// nhist = 7, glength
#define LOOPPREDICTOR                  //  use loop  predictor
#define LOCALH                         // use local histories
#define IMLI                           // using IMLI component
#define IMLISIC                        // use IMLI-SIC
#define IMLIOH                         // use IMLI-OH
#define DEFAULT_LOG2_TAGE_ENTRIES 12   /* logsize of the tagged TAGE tables*/
#define TBITS                     13   /* minimum tag width*/
#define MAXHIST                   200  // 200
#define MINHIST                   5
#endif

/*
#ifdef MEGA_IMLI
// use 20 tables (nhist = 20)
#define LOOPPREDICTOR //  use loop  predictor
#define LOCALH        // use local histories
#define IMLI          // using IMLI component
#define IMLISIC       // use IMLI-SIC
#define IMLIOH        // use IMLI-OH
#define DEFAULT_LOG2_TAGE_ENTRIES 13       // logsize of the tagged TAGE tables
#define TBITS 14      // minimum tag width
#define MAXHIST 400
#define MINHIST 5
#endif
*/
// To get the predictor storage budget on stderr  uncomment the next line

#define UWIDTH 1
#define CWIDTH 3

// use geometric history length
#ifndef MAXHIST
#ifdef USE_DOLC
#define MAXHIST 71
#define MINHIST 5
#else
// #define MINHIST 7
// #define MAXHIST 1000
#define MINHIST 1
#define MAXHIST 71
#endif
#endif
// probably not the best history length, but nice

#ifndef STRICTSIZE
#define PERCWIDTH 6  // Statistical corrector maximum counter width
#else
#define PERCWIDTH 7  // appears as a reasonably efficient way to use the last available bits
#endif
// The statistical corrector components from CBP4

// global branch GEHL
#ifdef LARGE_SC
#define LOGGNB 9
#else
#define LOGGNB 10
#endif

#ifdef IMLI
#ifdef STRICTSIZE
#define GNB 2
const int Gm[GNB] = {17, 14};
#else
#ifdef LARGE_SC
#define GNB 4
const int Gm[GNB] = {27, 22, 17, 14};
#else
#define GNB 2
const int Gm[GNB] = {17, 14};
#endif
#endif
#else
#ifdef LARGE_SC
#define GNB 4
const int Gm[GNB] = {27, 22, 17, 14};
#else
#define GNB 2
const int Gm[GNB] = {17, 14};
#endif

#endif
/*effective length is  -11,  we use (GHIST<<11)+IMLIcount; we force the IMLIcount zero when IMLI is not used*/

// large local history
#define LOGLOCAL 8
#define NLOCAL   (1 << LOGLOCAL)
#define INDLOCAL (orig_PC & (NLOCAL - 1))
#ifdef LARGE_SC
// three different local histories (just completely crazy :-)

#define LOGLNB 10
#define LNB    3
#else
// only one local history
#define LOGLNB 10
#define LNB    4
const int Lm[LNB] = {16, 11, 6, 3};
#endif

// small local history
#define LOGSECLOCAL 4
#define NSECLOCAL   (1 << LOGSECLOCAL)  // Number of second local histories
#define LOGSNB      9
#define SNB         4
const int Sm[SNB] = {16, 11, 6, 3};

// third local history
#define LOGTNB 9
#ifdef STRICTSIZE
#define TNB 2
const int Tm[TNB] = {17, 14};
#else
#define TNB 3
const int Tm[TNB] = {22, 17, 14};
#endif
// effective local history size +11: we use IMLIcount + (LH) << 11

#ifdef LARGE_SC
// return-stack associated history component
#ifdef STRICTSIZE
#define LOGPNB 8
#else
#define LOGPNB 9
#endif
#define PNB 4
const int Pm[PNB] = {16, 11, 6, 3};
#else
// in this case we don\B4t use the call stack
#define PNB    2
#define LOGPNB 11
const int Pm[PNB] = {16, 11};
#endif

// parameters of the loop predictor
#define LOGL            6
#define WIDTHNBITERLOOP 11  // we predict only loops with less than 1K iterations
#define LOOPTAG         12  // tag width in the loop predictor

// update threshold for the statistical corrector
#define LOGSIZEUP 0
#define INDUPD    (PC & ((1 << LOGSIZEUP) - 1))

// The three counters used to choose between TAGE ang SC on High Conf TAGE/Low Conf SC
#define CONFWIDTH  7   // for the counters in the choser
#define PHISTWIDTH 27  // width of the path history used in TAGE

#define HISTBUFFERLENGTH 4096  // we use a 4K entries history buffer to store the branch history

// the counter(s) to chose between longest match and alternate prediction on TAGE when weak counters
// #define LOGSIZEUSEALT 0
#define LOGSIZEUSEALT 2
#define SIZEUSEALT    (1 << (LOGSIZEUSEALT))
#define INDUSEALT     ((lastBoundaryPC) & (SIZEUSEALT - 1))

// utility class for index computation
// this is the cyclic shift register for folding
// a long global history into a smaller number of bits; see P. Michaud's PPM-like predictor at CBP-1
class folded_history {
public:
  unsigned comp;
  int      CLENGTH;
  int      OLENGTH;
  int      OUTPOINT;

  folded_history() {
    comp     = 0;
    CLENGTH  = 0;
    OLENGTH  = 0;
    OUTPOINT = 0;
  }

  void init(int original_length, int compressed_length) {
    comp     = 0;
    OLENGTH  = original_length;
    CLENGTH  = compressed_length;
    OUTPOINT = OLENGTH % CLENGTH;
  }

  void update(uint8_t* h, int PT) {
    comp = (comp << 1) ^ h[PT & (HISTBUFFERLENGTH - 1)];
    comp ^= h[(PT + OLENGTH) & (HISTBUFFERLENGTH - 1)] << OUTPOINT;
    comp ^= (comp >> CLENGTH);
    comp = (comp) & ((1 << CLENGTH) - 1);
  }

  void set(unsigned c) { comp = (c) & ((1 << CLENGTH) - 1); }
  void mix(unsigned c) { comp ^= (c) & ((1 << CLENGTH) - 1); }
};

#ifdef LOOPPREDICTOR
class lentry {
public:
  uint16_t NbIter;       // 10 bits
  uint8_t  confid;       // 4bits
  uint16_t CurrentIter;  // 10 bits

  uint16_t TAG;  // 10 bits
  uint8_t  age;  // 4 bits
  bool     dir;  // 1 bit

  // 39 bits per entry
  lentry() {
    confid      = 0;
    CurrentIter = 0;
    NbIter      = 0;
    TAG         = 0;
    age         = 0;
    dir         = false;
  }
};
#endif

class Bimodal {
private:
  const uint32_t bwidth;
  const uint32_t log2_size;
  const uint32_t log2_bimodal_nsub;
  const uint32_t indexMask;

  int pos_p;

  std::vector<int8_t> pred;

  uint32_t getIndex(uint32_t pc) const { return (pc & indexMask); }

public:
  Bimodal(int ls, int lbn, int bw) : bwidth(bw), log2_size(ls), log2_bimodal_nsub(lbn), indexMask((1 << (log2_size + lbn)) - 1) {
    pred.resize(1 << (log2_size + log2_bimodal_nsub), 0);

    pos_p = 0;
  }

  int getsize() const { return (1 << (log2_size + log2_bimodal_nsub)) * bwidth; }

  void dump() { printf(" loff=%d ctr=%d", pos_p, pred[pos_p]); }

  bool predict() const { return pred[pos_p] >= 0; }
  bool highconf() const { return (abs(2 * pred[pos_p] + 1) >= (1 << bwidth) - 1); }

  void select(uint32_t fetchPC_) { pos_p = getIndex(fetchPC_); }

  void select(uint32_t fetchPC_, uint8_t boff_) {
    pos_p = getIndex(getIndex((fetchPC_ << log2_bimodal_nsub)) + (boff_ & ((1 << log2_bimodal_nsub) - 1)));
  }

  void update(bool taken) {
    if (bwidth > 4) {
      if (taken && pred[pos_p] < -1) {
        pred[pos_p] = pred[pos_p] / 2;
      } else if (!taken && pred[pos_p] > 0) {
        pred[pos_p] = pred[pos_p] / 2;
      } else if (taken) {
        if (pred[pos_p] < ((1 << (bwidth - 1)) - 1)) {
          pred[pos_p]++;
        }
      } else {
        if (pred[pos_p] > -(1 << (bwidth - 1))) {
          pred[pos_p]--;
        }
      }
    } else {
      if (taken) {
        if (pred[pos_p] < ((1 << (bwidth - 1)) - 1)) {
          pred[pos_p]++;
        }
      } else {
        if (pred[pos_p] > -(1 << (bwidth - 1))) {
          pred[pos_p]--;
        }
      }
    }
  }
};

// TODO: Convert this class to GTable class that includes subtables inside
class gentry {
private:
  int8_t ctr_;
  int8_t u_;

public:
  uint32_t tag;
  bool     hit;

  gentry() : ctr_(0), u_(0), tag(0), hit(false) {}

  void allocate(int) {
    ctr_ = 0;
    u_   = 0;
    tag  = 0;
    hit  = false;
  }

  void dump() { printf("tag=%x hit=%d u=%d ctr=%d", tag, hit, u_, ctr_); }

  bool isHit() const { return hit; }
  bool isTagHit() const { return hit; }

    void select(Addr_t t) { hit = (t == tag); }

  void reset(uint32_t t, bool taken) {
    tag  = t;
    ctr_ = taken ? 0 : -1;
    hit  = true;
  }

  void ctr_force_steal(bool taken) {
    if (!hit) {
      return;
    }
    ctr_ = taken ? 0 : -1;
    u_dec();
  }

  bool ctr_steal([[maybe_unused]] bool taken) { return hit; }

  void ctr_update(bool taken) {
    if (!hit) {
      return;
    }

#if CWIDTH > 4
    if (taken && ctr_ < -1) {
      ctr_ = ctr_ / 2;
    } else if (!taken && ctr_ > 0) {
      ctr_ = ctr_ / 2;
    } else if (taken) {
      if (ctr_ < ((1 << (CWIDTH - 1)) - 1)) {
        ctr_++;
      }
    } else {
      if (ctr_ > -(1 << (CWIDTH - 1))) {
        ctr_--;
      }
    }
#else
    if (taken) {
      if (ctr_ < ((1 << (CWIDTH - 1)) - 1)) {
        ctr_++;
      }
    } else {
      if (ctr_ > -(1 << (CWIDTH - 1))) {
        ctr_--;
      }
    }
#endif
  }

    bool ctr_weak() const { return !hit || ctr_ == 0 || ctr_ == -1; }

  bool ctr_highconf() const { return hit && (abs(2 * ctr_ + 1) >= (1 << CWIDTH) - 1); }

    int ctr_get() const { return hit ? ctr_ : 0; }

  bool ctr_isTaken() const { return ctr_get() >= 0; }

  int u_get() const { return u_; }

  void u_inc() {
    if (u_ < (1 << UWIDTH) - 1) {
      u_++;
    }
  }

  void u_dec() {
    if (u_) {
      u_--;
    }
  }
    void u_clear() { u_ = 0; }
};

class IMLIBest {
public:
  Bimodal    bimodal;  // (log2_bimodal_entries,log2_bimodal_nsub,BWIDTH);
  const int  log2_bimodal_entries;
  const int  log2_bimodal_nsub;
  const int  bwidth;
  const int  nhist;
  const bool sc;
  const int  log2_tage_entries;
  const int  log2_tage_nsub;
  const int  tage_nsub_mask;  // (1 << log2_tage_nsub) - 1

  int get_tage_subentry(uint32_t tag) const {
    if (log2_tage_nsub == 0) {
      return 0;
    }

    return tag & tage_nsub_mask;
  }

  int get_tage_pos(int bank) const {
    int pos = GI[bank];
    if (log2_tage_nsub > 0) {
      pos = (pos << log2_tage_nsub) + get_tage_subentry(GTAG[bank]);
    }
    return pos;
  }

  gentry&       get_gentry(int bank) { return gtable[bank][get_tage_pos(bank)]; }
  const gentry& get_gentry(int bank) const { return gtable[bank][get_tage_pos(bank)]; }

#ifdef POSTPREDICT
#define POSTPEXTRA 2
#define POSTPBITS  5
#define CTRBITS    3  // Chop 2 bits

  uint32_t postpsize;
  int8_t*  postp;
  uint32_t ppi;

  uint32_t postp_index(uint32_t a, uint32_t b, uint32_t c) {
    int ctr[POSTPEXTRA + 1];
    if (a) {
      ctr[0] = get_gentry(a).ctr_get();
    } else {
      ctr[0] = bimodal.predict();
    }
    if (b) {
      ctr[1] = get_gentry(b).ctr_get();
    } else {
      ctr[1] = bimodal.predict();
    }
    if (c) {
      ctr[2] = get_gentry(c).ctr_get();
    } else {
      ctr[2] = bimodal.predict();
    }

    for (int i = 0; i < 3; i++) {
      if (ctr[i] >= 3) {
        ctr[i] = 3;
      } else if (ctr[i] <= -4) {
        ctr[i] = -4;
      }
    }

    int v = 0;
    for (int i = POSTPEXTRA; i >= 0; i--) {
      v = (v << CTRBITS) | (ctr[i] & (((1 << CTRBITS) - 1)));
    }
    int u0 = (a > 0) ? get_gentry(a).u_get() : 1;
    v      = (v << 1) | u0;
    v &= postpsize - 1;
    return v;
  }
#endif

  // IMLI related data declaration
  long long IMLIcount;
#define MAXIMLIcount 1023
#ifdef IMLI
#ifdef IMLISIC
// IMLI-SIC related data declaration
#define LOGINB 10  // (LOG of IMLI-SIC table size +1)
#define INB    1
#endif
// IMLI-OH related data declaration
#ifdef IMLIOH
  long long localoh;   // intermediate data to recover the two bits needed from the past outer iteration
#define SHIFTFUTURE 6  // (PC<<6) +IMLIcount to index the Outer History table
#define PASTSIZE    16
  int8_t PIPE[PASTSIZE];      // the PIPE vector
#define OHHISTTABLESIZE 1024  //
  int8_t ohhisttable[OHHISTTABLESIZE];
#ifdef STRICTSIZE
#define LOGFNB 7  // 64 entries
#else
#define LOGFNB 9  // 256 entries
#endif
#define FNB 1
  int Fm[FNB];
#endif
#endif

#ifndef POSTPREDICT
  int8_t use_alt_on_na[SIZEUSEALT][2];
#endif

  long long GHIST;
  long long phist;  // path history

// The two BIAS tables in the SC component
#define LOGBIAS 7
  int8_t Bias[(1 << (LOGBIAS + 1))];
#define INDBIAS ((((PC << 1) + pred_inter)) & ((1 << (LOGBIAS + 1)) - 1))
  int8_t BiasSK[(1 << (LOGBIAS + 1))];
#define INDBIASSK (((((PC ^ (PC >> LOGBIAS)) << 1) + pred_inter)) & ((1 << (LOGBIAS + 1)) - 1))

  bool HighConf;
  bool WeakConf;
  int  LSUM;

  int TICK;  // for the reset of the u counter

  uint8_t                     ghist[HISTBUFFERLENGTH];
  int                         ptghist;
  std::vector<folded_history> ch_i;     // [NHIST + 1];	//utility for computing TAGE indices
  std::vector<folded_history> ch_t[2];  // [NHIST + 1];	//utility for computing TAGE tags

  std::vector<std::vector<gentry>> gtable;  // [NHIST + 1];	// tagged TAGE tables

#ifdef IMLISIC
  int Im[INB];
#endif

  const int Lm[LNB] = {11, 6, 3};

#ifdef IMLIOH
  std::array<std::array<int8_t, (1 << LOGFNB)>, FNB> FGEHL;
  std::array<std::array<int8_t, (1 << LOGINB)>, INB> IGEHL;
#endif
  std::array<std::array<int8_t, (1 << LOGPNB)>, PNB> PGEHL;
  std::array<std::array<int8_t, (1 << LOGLNB)>, LNB> LGEHL;
  std::array<std::array<int8_t, (1 << LOGGNB)>, GNB> GGEHL;
  // int8_t  GGEHL[GNB][(1 << LOGGNB)];
  // int8_t  LGEHL[LNB][(1 << LOGLNB)];
  // int8_t  IGEHL[INB][(1 << LOGINB)];
  // int8_t  PGEHL[PNB][(1 << LOGPNB)];

  long long L_shist[NLOCAL];
  long long S_slhist[NSECLOCAL];
  long long T_slhist[NSECLOCAL];
  int       pthstack;

  int    Pupdatethreshold[(1 << LOGSIZEUP)];  // size is fixed by LOGSIZEUP
  int8_t FirstH, SecondH, ThirdH;

#ifdef USE_DOLC
  DOLC idolc(MAXHIST, 1, 6, 18);
#endif
  std::vector<int>  m;           // [NHIST + 1];	// history lengths
  std::vector<int>  TB;          //[NHIST + 1]; 	// tag width for the different tagged tables
  std::vector<int>  logg;        // [NHIST + 1];	// log of number entries of the different tagged tables
  std::vector<int>  GI;          //[NHIST + 1];		// indexes to the different tables are computed only once
  std::vector<uint> GTAG;        //[NHIST + 1];		// tags for the different tables are computed only once
  bool              pred_taken;  // prediction
  bool              alttaken;    // alternate  TAGEprediction
  bool              tage_pred;   // TAGE prediction
  bool              LongestMatchPred;
  int               HitBank;         // longest matching bank
  int               AltBank;         // alternate matching bank
  uint64_t          lastBoundaryPC;  // last PC that fetchBoundary was called
  uint64_t          imli_tag_offset;
  uint64_t          bim_tag_offset;
  uint64_t          lastBoundaryID;    // dinst ID for boundary to compute offset for all instruction types
  uint64_t          lastBoundarySign;  // lastBoundaryPC ^ PCs in not-taken in the branch bundle
  bool              lastBoundaryCtrl;
  int               Seed;  // for the pseudo-random number generator

  struct DeferredPredictionState {
    Addr_t            pc;
    std::vector<int>  gi;
    std::vector<uint> gtag;
    bool              advance_imli_tag_offset;
    bool              pred_taken;
    bool              alttaken;
    bool              tage_pred;
    bool              LongestMatchPred;
    int               HitBank;
    int               AltBank;
    bool              HighConf;
    int               LSUM;
    bool              pred_inter;
#ifdef LOOPPREDICTOR
    bool predloop;
    int  LIB;
    int  LI;
    int  LHIT;
    int  LTAG;
    bool LVALID;
#endif
#ifdef POSTPREDICT
    uint32_t ppi;
#endif
  };

  struct DeferredBoundaryOp {
    bool                    has_predictor_update;
    Addr_t                  orig_pc;
    Opcode                  brtype;
    bool                    taken;
    Addr_t                  target;
    bool                    no_alloc;
    DeferredPredictionState state;
  };
  std::vector<DeferredBoundaryOp> deferred_ops;

  bool pred_inter;

#ifdef LOOPPREDICTOR
  std::vector<lentry> ltable;  // loop predictor table
  // variables for the loop predictor
  bool   predloop;  // loop predictor prediction
  int    LIB;
  int    LI;
  int    LHIT;      // hitting way in the loop predictor
  int    LTAG;      // tag on the loop predictor
  bool   LVALID;    // validity of the loop predictor prediction
  int8_t WITHLOOP;  // counter to monitor whether or not loop prediction is beneficial
#endif

  IMLIBest(int _log2_bimodal_nsub, int _log2_bimodal_entries, int _bwidth, int _nhist, bool _sc, int _log2_tage_entries,
           int _log2_tage_nsub = 0)
      : bimodal(_log2_bimodal_entries, _log2_bimodal_nsub, _bwidth)
      , log2_bimodal_entries(_log2_bimodal_entries)
      , log2_bimodal_nsub(_log2_bimodal_nsub)
      , bwidth(_bwidth)
      , nhist(_nhist >= MAXHIST ? MAXHIST : _nhist)
      , sc(_sc)
      , log2_tage_entries(_log2_tage_entries)
      , log2_tage_nsub(_log2_tage_nsub)
      , tage_nsub_mask((1 << _log2_tage_nsub) - 1) {
    ch_i.resize(nhist + 1);
    ch_t[0].resize(nhist + 1);
    ch_t[1].resize(nhist + 1);

    gtable.resize(nhist + 1);
    m.resize(nhist + 1);
    TB.resize(nhist + 1);
    logg.resize(nhist + 1);
    GI.resize(nhist + 1);
    GTAG.resize(nhist + 1);

    reinit();
    predictorsize();
  }

  int predictorsize() {
    int STORAGESIZE = 0;
    int inter       = 0;

    for (int i = 1; i <= nhist; i += 1) {
      int s = logg[i];
      int x = (1 << (s + log2_tage_nsub)) * (CWIDTH + UWIDTH + TB[i]);
      fprintf(stderr,
              "table[%d] size=%d log2entries=%d log2nsub=%d histlength=%d taglength=%d\n",
              i,
              x,
              s,
              log2_tage_nsub,
              m[i],
              TB[i]);

      STORAGESIZE += x;
    }

    STORAGESIZE += 2 * (SIZEUSEALT) * 4;
    fprintf(stderr, " altna size=%d log2entries=%d\n", 2 * (SIZEUSEALT) * 4, LOGSIZEUSEALT);

    inter = bwidth * (1 << (log2_bimodal_nsub + log2_bimodal_entries));
    fprintf(stderr, " bimodal table bit_size=%d log2entries=%d log2nsub=%d\n", inter, log2_bimodal_entries, log2_bimodal_nsub);

    STORAGESIZE += inter;
    STORAGESIZE += m[nhist];
    STORAGESIZE += PHISTWIDTH;
    STORAGESIZE += 10;  // the TICK counter

    fprintf(stderr, " (TAGE %d) ", STORAGESIZE);

#ifdef LOOPPREDICTOR
    inter = (1 << LOGL) * (2 * WIDTHNBITERLOOP + LOOPTAG + 4 + 4 + 1);
    fprintf(stderr, " (LOOP %d) ", inter);
    STORAGESIZE += inter;
#endif

    if (sc) {
      inter = 0;

      inter += 16;                   // global histories for SC
      inter = 8 * (1 << LOGSIZEUP);  // the update threshold counters
      inter += (PERCWIDTH) * 4 * (1 << (LOGBIAS));
      inter += (GNB - 2) * (1 << (LOGGNB)) * (PERCWIDTH - 1) + (1 << (LOGGNB - 1)) * (2 * PERCWIDTH - 1);

      inter += (PNB - 2) * (1 << (LOGPNB)) * (PERCWIDTH - 1) + (1 << (LOGPNB - 1)) * (2 * PERCWIDTH - 1);

#ifdef LOCALH
      inter += (LNB - 2) * (1 << (LOGLNB)) * (PERCWIDTH - 1) + (1 << (LOGLNB - 1)) * (2 * PERCWIDTH - 1);
      inter += NLOCAL * Lm[0];

      inter += (SNB - 2) * (1 << (LOGSNB)) * (PERCWIDTH - 1) + (1 << (LOGSNB - 1)) * (2 * PERCWIDTH - 1);
      inter += (TNB - 2) * (1 << (LOGTNB)) * (PERCWIDTH - 1) + (1 << (LOGTNB - 1)) * (2 * PERCWIDTH - 1);
      inter += 16 * 16;  // the history stack
      inter += 4;        // the history stack pointer

      inter += NSECLOCAL * Sm[0];
      inter += NSECLOCAL * (Tm[0] - 11);
      /* Tm[0] is artificially increased by 11 to accomodate IMLI*/

#endif

#ifdef IMLI
#ifdef IMLIOH
      inter += OHHISTTABLESIZE;
      inter += PASTSIZE;
      /*the PIPE table*/
      // in cases you add extra tables to IMLI OH, the formula is correct
      switch (FNB) {
        case 1: inter += (1 << (LOGFNB - 1)) * PERCWIDTH; break;
        default: inter += (FNB - 2) * (1 << (LOGFNB)) * (PERCWIDTH - 1) + (1 << (LOGFNB - 1)) * (2 * PERCWIDTH - 1);
      }
#endif
#ifdef IMLISIC  // in cases you add extra tables to IMLI SIC, the formula is correct
      switch (INB) {
        case 1: inter += (1 << (LOGINB - 1)) * PERCWIDTH; break;

        default: inter += (INB - 2) * (1 << (LOGINB)) * (PERCWIDTH - 1) + (1 << (LOGINB - 1)) * (2 * PERCWIDTH - 1);
      }
#endif

#endif

      inter += 3 * CONFWIDTH;  // the 3 counters in the choser
      STORAGESIZE += inter;

      fprintf(stderr, " (SC %d) ", inter);
    }

    fprintf(stderr, " (TOTAL %d) ", STORAGESIZE);

    return (STORAGESIZE);
  }

  void reinit() {
#ifdef POSTPREDICT
    postpsize = 1 << ((1 + POSTPEXTRA) * CTRBITS + 1);
    postp     = new int8_t[postpsize];
    for (int i = 0; i < postpsize; i++) {
      postp[i] = -(((i >> 1) >> (CTRBITS - 1)) & 1);
    }
#endif

#ifdef IMLISIC
    for (int i = 0; i < INB; i++) {
      Im[i] = 10;  // the IMLIcounter is limited to 10 bits
    }
#endif

#ifdef IMLIOH
    localoh = 0;
    for (int i = 0; i < PASTSIZE; i++) {
      PIPE[i] = 0;
    }
    for (int i = 0; i < OHHISTTABLESIZE; i++) {
      ohhisttable[i] = 0;
    }
    for (int i = 0; i < FNB; i++) {
      Fm[i] = 2;
    }
#endif

    m[1]     = MINHIST;
    m[nhist] = MAXHIST;
    for (int i = 2; i <= nhist; i++) {
      if (MAXHIST <= nhist) {
        m[i] = i;
      } else {
        m[i] = (int)(((double)MINHIST * pow((double)(MAXHIST) / (double)MINHIST, (double)(i - 1) / (double)((nhist - 1)))) + 0.5);
      }
    }

    for (int i = 1; i <= nhist; i++) {
      TB[i]   = TBITS + (i / 2);
      logg[i] = log2_tage_entries;
    }

#ifdef LOOPPREDICTOR
    ltable.resize(1 << (LOGL));
#endif

    // int galloc[]= {0, 6, 6, 5, 5, 4, 4, 3, 3, 2, 2};
    // int ngalloc =9;
    int galloc[] = {0, 1, 1, 1, 1};
    int ngalloc  = 3;

    for (int i = 1; i <= nhist; i++) {
      gtable[i].resize(1 << (logg[i] + log2_tage_nsub));
      for (int j = 0; j < (1 << (logg[i] + log2_tage_nsub)); j++) {
        int s;
        if (i >= ngalloc) {
          s = galloc[ngalloc];
        } else {
          s = galloc[i];
        }
        gtable[i][j].allocate(s);
        // printf("IMLIBEST::reinit()::gtable[%d][%d].allocate() at clock cycle %llu\n", i, j, globalClock);
      }
    }

    for (int i = 1; i <= nhist; i++) {
      ch_i[i].init(m[i], (logg[i]));
      ch_t[0][i].init(ch_i[i].OLENGTH, TB[i]);
      ch_t[1][i].init(ch_i[i].OLENGTH, TB[i] - 1);
    }
#ifdef LOOPPREDICTOR
    LVALID   = false;
    WITHLOOP = -1;
#endif
    Seed = 0;

    TICK  = 0;
    phist = 0;
    Seed  = 0;

    for (int i = 0; i < HISTBUFFERLENGTH; i++) {
      ghist[i] = 0;
    }
    ptghist = 0;

    for (int i = 0; i < (1 << LOGSIZEUP); i++) {
      Pupdatethreshold[i] = 35;
    }

    for (int i = 0; i < GNB; i++) {
      for (int j = 0; j < ((1 << LOGGNB) - 1); j++) {
        if (!(j & 1)) {
          GGEHL[i][j] = -1;
        }
      }
    }
    for (int i = 0; i < LNB; i++) {
      for (int j = 0; j < ((1 << LOGLNB) - 1); j++) {
        if (!(j & 1)) {
          LGEHL[i][j] = -1;
        }
      }
    }
    for (int i = 0; i < PNB; i++) {
      for (int j = 0; j < ((1 << LOGPNB) - 1); j++) {
        if (!(j & 1)) {
          PGEHL[i][j] = -1;
        }
      }
    }

#ifdef IMLI
#ifdef IMLIOH
    for (int i = 0; i < FNB; i++) {
      for (int j = 0; j < ((1 << LOGFNB) - 1); j++) {
        if (!(j & 1)) {
          FGEHL[i][j] = -1;
        }
      }
    }
#endif
#ifdef IMLISIC
    for (int i = 0; i < INB; i++) {
      for (int j = 0; j < ((1 << LOGINB) - 1); j++) {
        if (!(j & 1)) {
          IGEHL[i][j] = -1;
        }
      }
    }
#endif
#endif

    for (int j = 0; j < (1 << (LOGBIAS + 1)); j++) {
      Bias[j] = (j & 1) ? 15 : -16;
    }
    for (int j = 0; j < (1 << (LOGBIAS + 1)); j++) {
      BiasSK[j] = (j & 1) ? 15 : -16;
    }

    for (int i = 0; i < NLOCAL; i++) {
      L_shist[i] = 0;
    }

    for (int i = 0; i < NSECLOCAL; i++) {
      S_slhist[i] = 0;
      T_slhist[i] = 0;
    }
    GHIST     = 0;
    IMLIcount = 0;
    pthstack  = 0;
    FirstH    = 0;
    SecondH   = 0;
    ThirdH    = 0;

#ifndef POSTPREDICT
    for (int i = 0; i < SIZEUSEALT; i++) {
      use_alt_on_na[i][0] = 0;
      use_alt_on_na[i][1] = 0;
    }
#endif

    TICK    = 0;
    ptghist = 0;
    phist   = 0;
  }
  // index function for the bimodal table

  // the index functions for the tagged tables uses path history as in the OGEHL predictor
  // F serves to mix path history: not very important impact

  int F(long long A, int size, int bank) {
    int A1, A2;
    A  = A & ((1 << size) - 1);
    A1 = (A & ((1 << logg[bank]) - 1));
    A2 = (A >> logg[bank]);
    A2 = ((A2 << bank) & ((1 << logg[bank]) - 1)) + (A2 >> (logg[bank] - bank));
    A  = A1 ^ A2;
    A  = ((A << bank) & ((1 << logg[bank]) - 1)) + (A >> (logg[bank] - bank));
    return (A);
  }

  int gindex(unsigned int PC, int bank, long long hist) {
    int index;
#ifdef USE_DOLC
    // Dual bank per bank (lower bit is PC based)
    uint64_t sign1 = idolc.getSign(logg[bank], m[bank]);
    index          = PC ^ (PC >> (bank + 1)) ^ (sign1);
#else
    int M = (m[bank] > PHISTWIDTH) ? PHISTWIDTH : m[bank];
    index = PC ^ (PC >> (abs(logg[bank] - bank) + 1)) ^ ch_i[bank].comp ^ F(hist, M, bank);
#endif
    return (index & ((1 << (logg[bank])) - 1));
  }

  // up-down saturating counter
  void ctrupdate(int8_t& ctr, bool taken, int nbits) {
    if (taken) {
      if (ctr < ((1 << (nbits - 1)) - 1)) {
        ctr++;
      }
    } else {
      if (ctr > -(1 << (nbits - 1))) {
        ctr--;
      }
    }
  }

#ifdef LOOPPREDICTOR
  int lindex(Addr_t PC) { return ((PC & ((1 << (LOGL - 2)) - 1)) << 2); }

// loop prediction: only used if high confidence
// skewed associative 4-way
// At fetch time: speculative
#define CONFLOOP 15
  bool getloop(Addr_t PC) {
    LHIT = -1;

    LI   = lindex(PC);
    LIB  = ((PC >> (LOGL - 2)) & ((1 << (LOGL - 2)) - 1));
    LTAG = (PC >> (LOGL - 2)) & ((1 << 2 * LOOPTAG) - 1);
    LTAG ^= (LTAG >> LOOPTAG);
    LTAG = (LTAG & ((1 << LOOPTAG) - 1));

    for (int i = 0; i < 4; i++) {
      int index = (LI ^ ((LIB >> i) << 2)) + i;

      if (ltable[index].TAG == LTAG) {
        LHIT   = i;
        LVALID = ((ltable[index].confid == CONFLOOP) || (ltable[index].confid * ltable[index].NbIter > 128));
        {
        }
        if (ltable[index].CurrentIter + 1 == ltable[index].NbIter) {
          return (!(ltable[index].dir));
        } else {
          return ((ltable[index].dir));
        }
      }
    }

    LVALID = false;
    return (false);
  }

  void loopupdate(bool Taken, bool ALLOC) {
    if (LHIT >= 0) {
      int index = (LI ^ ((LIB >> LHIT) << 2)) + LHIT;
      // already a hit
      if (LVALID) {
        if (Taken != predloop) {
          // free the entry
          ltable[index].NbIter      = 0;
          ltable[index].age         = 0;
          ltable[index].confid      = 0;
          ltable[index].CurrentIter = 0;
          return;

        } else if ((predloop != tage_pred) || ((MYRANDOM() & 7) == 0)) {
          if (ltable[index].age < CONFLOOP) {
            ltable[index].age++;
          }
        }
      }

      ltable[index].CurrentIter++;
      ltable[index].CurrentIter &= ((1 << WIDTHNBITERLOOP) - 1);
      // loop with more than 2** WIDTHNBITERLOOP iterations are not treated correctly; but who cares :-)
      if (ltable[index].CurrentIter > ltable[index].NbIter) {
        ltable[index].confid = 0;
        ltable[index].NbIter = 0;
        // treat like the 1st encounter of the loop
      }
      if (Taken != ltable[index].dir) {
        if (ltable[index].CurrentIter == ltable[index].NbIter) {
          if (ltable[index].confid < CONFLOOP) {
            ltable[index].confid++;
          }
          if (ltable[index].NbIter < 3)
          // just do not predict when the loop count is 1 or 2
          {
            // free the entry
            ltable[index].dir    = Taken;
            ltable[index].NbIter = 0;
            ltable[index].age    = 0;
            ltable[index].confid = 0;
          }
        } else {
          if (ltable[index].NbIter == 0) {
            // first complete nest;
            ltable[index].confid = 0;
            ltable[index].NbIter = ltable[index].CurrentIter;
          } else {
            // not the same number of iterations as last time: free the entry
            ltable[index].NbIter = 0;
            ltable[index].confid = 0;
          }
        }
        ltable[index].CurrentIter = 0;
      }

    } else if (ALLOC)

    {
      Addr_t X = MYRANDOM() & 3;

      if ((MYRANDOM() & 3) == 0) {
        for (int i = 0; i < 4; i++) {
          int LHIT2 = (X + i) & 3;
          int index = (LI ^ ((LIB >> LHIT2) << 2)) + LHIT2;
          if (ltable[index].age == 0) {
            ltable[index].dir = !Taken;
            // most of mispredictions are on last iterations
            ltable[index].TAG         = LTAG;
            ltable[index].NbIter      = 0;
            ltable[index].age         = 7;
            ltable[index].confid      = 0;
            ltable[index].CurrentIter = 0;
            break;

          } else {
            ltable[index].age--;
          }
          break;
        }
      }
    }
  }
#endif

  // just a simple pseudo random number generator: use available information
  // to allocate entries  in the loop predictor
  int MYRANDOM() {
    Seed++;
    Seed ^= phist;
    Seed = (Seed >> 21) + (Seed << 11);

    return Seed;
  }

  void setTAGEIndex() {
    HitBank = 0;
    AltBank = 0;

    GI[0] = lastBoundaryPC;  // already a hash >> 2;  // Remove 2 lower useless bits
    for (int i = 1; i <= nhist; i++) {
      GI[i] = gindex(lastBoundaryPC, i, phist);
    }
  }

  void setTAGETag(Addr_t PC) {
    HitBank = 0;
    AltBank = 0;

    for (int i = 1; i <= nhist; i++) {
      auto tag = imli_bpred_hash(gindex(PC, i - 1, phist), PC);
      GTAG[i]  = tag & ((1 << TB[i]) - 1);
    }
  }

  void setTAGEPred() {
    // printf("IMLIBEST::setTAGEPRED():: Entering at clock cycle %llu\n", globalClock);
    HitBank = 0;
    AltBank = 0;
    // printf("IMLIBEST::setTAGEPRED():: nhist is %d at clock cycle %llu\n", nhist, globalClock);
    for (int i = 1; i <= nhist; i++) {
            if (get_gentry(i).isHit()) {
        LongestMatchPred = get_gentry(i).ctr_isTaken();
        HitBank = i;
        // printf("IMLIBEST::setTAGEPred::gtable[%d][GI[%d]]:: HitBank is %d  at clock cycle %llu\n", i, i, HitBank, globalClock);
      }
    }

    // printf("IMLIBEST::setTAGEPred::LongestMAtchPred is %b  at clock cycle %llu\n", LongestMatchPred, globalClock);
    for (int i = HitBank - 1; i > 0; i--) {
      if (get_gentry(i).isHit()) {
        AltBank = i;
        // printf("IMLIBEST::setTAGEPred::gtable[%d][GI[%d]]:: AltBank is %d  at clock cycle %llu\n", i, i, AltBank, globalClock);
        break;
      }
    }

#ifdef POSTPREDICT
    int WeakBank = 0;
    for (int i = AltBank - 1; i > 0; i--) {
      if (get_gentry(i).isHit()) {
        WeakBank = i;
        break;
      }
    }

    if (HitBank > 0) {
      if (AltBank > 0) {
                alttaken = get_gentry(AltBank).ctr_isTaken();
      } else {
        alttaken = bimodal.predict();
        // printf("IMLIBEST::setTAGEPred::HITBANK>0::ALTBANK<= 0:: alttaken ::altaken  is %b  at clock cycle %llu\n",
               // alttaken,
               // globalClock);
      }
    } else {
      alttaken         = bimodal.predict();
      LongestMatchPred = alttaken;
      // printf("IMLIBEST::setTAGEPred::HITBank<=0::longestmatchtaken ::altaken  is %b  at clock cycle %llu\n",
             // LongestMatchPred,
             // globalClock);
    }
#else
    // computes the prediction and the alternate prediction
    if (HitBank > 0) {
      if (AltBank > 0) {
                alttaken = get_gentry(AltBank).ctr_isTaken();
      } else {
        alttaken = bimodal.predict();
        // printf("IMLIBEST::setTAGEPred::HITBANK>0:ALTBANk<=0::BIMODAL_PREDICT::alttaken ::altaken  is %b  at clock cycle %llu\n",
               // alttaken,
               // globalClock);
      }

      // if the entry is recognized as a newly allocated entry and
      // USE_ALT_ON_NA is positive  use the alternate prediction
      int  index          = INDUSEALT ^ LongestMatchPred;
      bool Huse_alt_on_na = (use_alt_on_na[index][HitBank > (nhist / 3)] >= 0);

      if (!Huse_alt_on_na || !get_gentry(HitBank).ctr_weak()) {
        tage_pred = LongestMatchPred;
                HighConf = get_gentry(HitBank).ctr_highconf();
        WeakConf = get_gentry(HitBank).ctr_weak();
      } else {
        tage_pred = alttaken;
        // printf("IMLIBEST::setTAGEPred:: gtable[HitBank][GI[HitBank]].ctr_weak())::tage_pred  is %b  at clock cycle %llu\n",
               // tage_pred,
               // globalClock);
        if (AltBank) {
          HighConf = get_gentry(AltBank).ctr_highconf();
          WeakConf = get_gentry(AltBank).ctr_weak();
        } else {
          HighConf = bimodal.highconf();
          WeakConf = !HighConf;
        }
      }
    } else {
      HighConf  = bimodal.highconf();
      WeakConf  = !HighConf;
      alttaken  = bimodal.predict();
      tage_pred = alttaken;
      // printf("IMLIBEST::setTAGEPred::HitBank<=0::tage_pred  is %b  at clock cycle %llu\n", tage_pred, globalClock);
      LongestMatchPred = alttaken;
    }
#endif

#ifdef POSTPREDICT
    ppi = postp_index(HitBank, AltBank, WeakBank);
    I(ppi < postpsize);
    // printf("postp[%d]=%d\n", ppi, postp[ppi]);
    tage_pred = (postp[ppi] >= 0);
#endif
  }
  // compute the prediction

  void fetchBoundaryBegin(Addr_t PC, uint64_t ID) {
    lastBoundaryPC  = imli_bpred_hash(PC);
    imli_tag_offset = 0;
    bim_tag_offset  = 0;
#ifdef SIMPLER_DOLC_PATH
    lastBoundarySign = PC;
#else
    lastBoundarySign = 0;
#endif
    lastBoundaryCtrl = false;

    lastBoundaryID = ID;

    I(deferred_ops.empty());
    setTAGEIndex();
  }

  void fetchBoundaryEnd() {
#ifdef USE_DOLC
    if (lastBoundaryCtrl) {
      idolc.update(lastBoundarySign);
    }
#endif
    for (auto& e : deferred_ops) {
      if (e.has_predictor_update) {
        applyDeferredPredictorUpdate(e);
      }

      Addr_t orig_PC = e.orig_pc;  // needed by INDLOCAL macro
      HistoryUpdate(orig_PC, e.brtype, e.taken, e.target, phist, ptghist, ch_i, ch_t[0], ch_t[1], L_shist[INDLOCAL], GHIST);
      setTAGEIndex();
    }
    deferred_ops.clear();
  }

  uint32_t dohash(uint32_t addr, uint16_t offset) {
    uint32_t sign = (addr << 1) ^ offset;

    return sign;
  }
  // #define IDEAL_REHASH_BIM_BOUNDARY 1

  void select_tage_entries(Addr_t orig_PC, uint64_t orig_ID) {
    (void)orig_ID;
    (void)orig_PC;

#ifdef IDEAL_BOUNDARY_ONLY_FOR_CTRL
    bimodal.select(GI[0], bim_tag_offset);
#elifdef IDEAL_REHASH_BIM_BOUNDARY
    bimodal.select(imli_bpred_hash(orig_PC));
#else
    bimodal.select(GI[0], imli_bpred_hash(lastBoundaryPC, 100 + orig_ID - lastBoundaryID));
#endif

    for (int i = 1; i <= nhist; i++) {
      get_gentry(i).select(GTAG[i]);
    }
  }

    bool getPrediction(Addr_t orig_PC, uint64_t orig_ID, bool& bias, uint32_t& sign, bool use_tag_offset, bool use_tag_hybrid,
                     uint32_t taken_counter) {
    bool force_offset = (taken_counter >= 1 && use_tag_hybrid);

    Addr_t PC;
    if (use_tag_offset || force_offset) {
#ifdef IDEAL_BOUNDARY_ONLY_FOR_CTRL
      PC = imli_bpred_hash(lastBoundaryPC, 100 + imli_tag_offset);
#else
      PC = imli_bpred_hash(lastBoundaryPC, 100 + orig_ID - lastBoundaryID);
#endif
    } else {
      PC = imli_bpred_hash(orig_PC);
    }

    // fetchBoundaryBegin(orig_PC, orig_ID);
    setTAGETag(PC);

    select_tage_entries(orig_PC, orig_ID);

    setTAGEPred();

    pred_taken = tage_pred;

    bias = HighConf;
    sign = GI[1];

#ifdef LOOPPREDICTOR
    predloop   = getloop(PC);  // loop prediction
    pred_taken = ((WITHLOOP >= 0) && (LVALID)) ? predloop : pred_taken;
    if ((WITHLOOP >= 0) && (LVALID)) {
      bias = true;
    }
#endif

    pred_inter = pred_taken;

    if (!sc) {
      // printf("IMLIBEST::getpredict:: !SC return  pred_taken is %b  at clock cycle %llu\n", pred_taken, globalClock);
      return (pred_taken);
    }

    // Compute the SC prediction

    LSUM = 0;
    ////// Very marginal effect
    // begin to bias the sum towards TAGE predicted direction
    LSUM = 1;

    LSUM += (2 * PNB);

#ifdef LOCALH
    LSUM += (2 * LNB);
#endif
#ifdef IMLI
    LSUM += 8;
#endif

    if (!pred_inter) {
      LSUM = -LSUM;
    }
    ////////////////////////////////////
    // integrate BIAS prediction
    int8_t ctr = Bias[INDBIAS];
    LSUM += (2 * ctr + 1);
    ctr = BiasSK[INDBIASSK];
    LSUM += (2 * ctr + 1);

// integrate the GEHL predictions
#ifdef IMLI
#ifdef IMLIOH
    localoh = 0;
    localoh = PIPE[(PC ^ (PC >> 4)) & (PASTSIZE - 1)] + (localoh << 1);
    for (int i = 0; i >= 0; i--) {
      localoh = ohhisttable[(((PC ^ (PC >> 4)) << SHIFTFUTURE) + IMLIcount + i) & (OHHISTTABLESIZE - 1)] + (localoh << 1);
    }

    if (IMLIcount >= 2) {
      LSUM += 2 * Gpredict<LOGFNB, FNB>(FGEHL);
    }
#endif

#ifdef IMLISIC
    LSUM += 2 * Gpredict<LOGINB, INB>(IGEHL);
#else
    long long interIMLIcount = IMLIcount;
    /* just a trick to disable IMLIcount*/
    IMLIcount = 0;
#endif
#endif

#ifndef IMLI
    IMLIcount = 0;
#endif

    LSUM += Gpredict<LOGGNB, GNB>(GGEHL);

#ifdef LOCALH
    LSUM += Gpredict<LOGLNB, LNB>(LGEHL);
#endif
#ifdef IMLI
#ifndef IMLISIC
    IMLIcount = interIMLIcount;
#endif
#endif

    LSUM += Gpredict<LOGPNB, PNB>(PGEHL);

    bool SCPRED = (LSUM >= 0);

    // chose between the SC output and the TAGE + loop  output

    if (pred_inter != SCPRED) {
      // Choser uses TAGE confidence and |LSUM|
      pred_taken = SCPRED;
      if (HighConf) {
        if ((abs(LSUM) < Pupdatethreshold[INDUPD] / 3)) {
          pred_taken = (FirstH < 0) ? SCPRED : pred_inter;
        }

        else if ((abs(LSUM) < 2 * Pupdatethreshold[INDUPD] / 3)) {
          pred_taken = (SecondH < 0) ? SCPRED : pred_inter;
        } else if ((abs(LSUM) < Pupdatethreshold[INDUPD])) {
          pred_taken = (ThirdH < 0) ? SCPRED : pred_inter;
        }
      }
    }

    if (pred_taken == tage_pred && HighConf) {
      bias = true;
    }

    // printf("IMLIBEST::getpredict::RETURN at LAST pred_taken is %b  at clock cycle %llu\n", pred_taken, globalClock);
    return pred_taken;
  }  // get_prediction_end

  /*Update History*/
  void HistoryUpdate(Addr_t PC, Opcode brtype, bool taken, Addr_t target, long long& X, int& Y, std::vector<folded_history>& H,
                     std::vector<folded_history>& G, std::vector<folded_history>& J, long long& LH, long long& GBRHIST) {
    // special treatment for unconditional branchs;
    int maxt;
    if (brtype == Opcode::iBALU_LBRANCH) {
      maxt = 1;
    } else {
      maxt = 4;
    }

    // the return stack associated history

#ifdef IMLI
    if (brtype == Opcode::iBALU_LBRANCH) {
      if (target < PC) {
        // This branch is a branch "loop"
        if (!taken) {
          // exit of the "loop"
          IMLIcount = 0;
        }
        if (taken) {
          if (IMLIcount < (MAXIMLIcount)) {
            IMLIcount++;
          }
        }
      }
    }
#ifdef IMLIOH
    if (IMLIcount >= 1) {
      if (brtype == Opcode::iBALU_LBRANCH) {
        if (target >= PC) {
          PIPE[(PC ^ (PC >> 4)) & (PASTSIZE - 1)]
              = ohhisttable[(((PC ^ (PC >> 4)) << SHIFTFUTURE) + IMLIcount) & (OHHISTTABLESIZE - 1)];
          ohhisttable[(((PC ^ (PC >> 4)) << SHIFTFUTURE) + IMLIcount) & (OHHISTTABLESIZE - 1)] = taken;
        }
      }
    }
#endif
#endif

    if (brtype == Opcode::iBALU_LBRANCH) {
      GBRHIST = (GBRHIST << 1) + taken;
      LH      = (LH << 1) + (taken);
    }

#ifdef USE_DOLC
    for (int i = 1; i <= nhist; i++) {
      uint64_t sign1 = 0;  // dolc.getSignInt(pcSign(PC), logg[i], m[i]);
      uint64_t sign2 = 0;  // dolc.getSign(TB[i]  , m[i]);
      H[i].set(sign1);
      G[i].set(sign2);  // Not used in DOLC
      J[i].set(sign2);  // Not used in DOLC
    }
#else
    int T    = ((PC) << 1) + taken;
    int PATH = PC;
#endif

    for (int t = 0; t < maxt; t++) {
#ifdef USE_DOLC
      ghist[Y & (HISTBUFFERLENGTH - 1)] = 0;
      X                                 = 0;
      Y--;
#else
      bool DIR = (T & 1);
      T >>= 1;
      int PATHBIT = (PATH & 127);
      PATH >>= 1;
      // update  history
      Y--;
      ghist[Y & (HISTBUFFERLENGTH - 1)] = DIR;
      X                                 = (X << 1) ^ PATHBIT;
      for (int i = 1; i <= nhist; i++) {
        H[i].update(ghist, Y);
        G[i].update(ghist, Y);
        J[i].update(ghist, Y);
      }
#endif
    }

  }  // HISTORY_UPDATE_END

  // PREDICTOR UPDATE

  DeferredPredictionState captureDeferredPredictionState(Addr_t PC) const {
    DeferredPredictionState state{
        .pc                      = PC,
        .gi                      = GI,
        .gtag                    = GTAG,
        .advance_imli_tag_offset = false,
        .pred_taken              = pred_taken,
        .alttaken                = alttaken,
        .tage_pred               = tage_pred,
        .LongestMatchPred        = LongestMatchPred,
        .HitBank                 = HitBank,
        .AltBank                 = AltBank,
        .HighConf                = HighConf,
        .LSUM                    = LSUM,
        .pred_inter              = pred_inter,
#ifdef LOOPPREDICTOR
        .predloop = predloop,
        .LIB      = LIB,
        .LI       = LI,
        .LHIT     = LHIT,
        .LTAG     = LTAG,
        .LVALID   = LVALID,
#endif
#ifdef POSTPREDICT
        .ppi = ppi,
#endif
    };

    return state;
  }

  void deferPredictorUpdate(Addr_t PC, uint64_t ID, bool resolveDir, bool predDir, Addr_t branchTarget, bool no_alloc,
                            bool use_tag_offset, bool use_tag_hybrid, uint32_t taken_counter) {
    (void)predDir;
    Addr_t orig_PC      = PC;
    bool   force_offset = (taken_counter >= 1 && use_tag_hybrid);
    if (use_tag_offset || force_offset) {
#ifdef IDEAL_BOUNDARY_ONLY_FOR_CTRL
      PC = imli_bpred_hash(lastBoundaryPC, imli_tag_offset);
#else
      PC = imli_bpred_hash(lastBoundaryPC, ID - lastBoundaryID);
#endif
    } else {
      PC = imli_bpred_hash(PC);
    }

    auto state                    = captureDeferredPredictionState(PC);
    state.advance_imli_tag_offset = use_tag_offset || force_offset;

    deferred_ops.push_back({
        .has_predictor_update = true,
        .orig_pc              = orig_PC,
        .brtype               = Opcode::iBALU_LBRANCH,
        .taken                = resolveDir,
        .target               = branchTarget,
        .no_alloc             = no_alloc,
        .state                = std::move(state),
    });
  }

  void applyDeferredPredictorUpdate(const DeferredBoundaryOp& e) {
    const auto& state = e.state;
    Addr_t      PC    = state.pc;

    GI               = state.gi;
    GTAG             = state.gtag;
    pred_taken       = state.pred_taken;
    alttaken         = state.alttaken;
    tage_pred        = state.tage_pred;
    LongestMatchPred = state.LongestMatchPred;
    HitBank          = state.HitBank;
    AltBank          = state.AltBank;
    HighConf         = state.HighConf;
    LSUM             = state.LSUM;
    pred_inter       = state.pred_inter;
#ifdef LOOPPREDICTOR
    predloop = state.predloop;
    LIB      = state.LIB;
    LI       = state.LI;
    LHIT     = state.LHIT;
    LTAG     = state.LTAG;
    LVALID   = state.LVALID;
#endif
#ifdef POSTPREDICT
    ppi = state.ppi;
#endif


#ifdef LOOPPREDICTOR
    if (LVALID) {
      if (pred_taken != predloop) {
        ctrupdate(WITHLOOP, (predloop == e.taken), 7);
      }
    }

    loopupdate(e.taken, (pred_taken != e.taken));
#endif

    if (sc) {
      bool SCPRED = (LSUM >= 0);
      if (HighConf) {
        if (pred_inter != SCPRED) {
          if ((abs(LSUM) < Pupdatethreshold[INDUPD])) {
            if ((abs(LSUM) < Pupdatethreshold[INDUPD] / 3)) {
              ctrupdate(FirstH, (pred_inter == e.taken), CONFWIDTH);
            } else if ((abs(LSUM) < 2 * Pupdatethreshold[INDUPD] / 3)) {
              ctrupdate(SecondH, (pred_inter == e.taken), CONFWIDTH);
            } else if ((abs(LSUM) < Pupdatethreshold[INDUPD])) {
              ctrupdate(ThirdH, (pred_inter == e.taken), CONFWIDTH);
            }
          }
        }
      }

      if ((SCPRED != e.taken) || ((abs(LSUM) < Pupdatethreshold[INDUPD]))) {
        if (SCPRED != e.taken) {
          Pupdatethreshold[INDUPD] += 1;
        } else {
          Pupdatethreshold[INDUPD] -= 1;
        }

        if (Pupdatethreshold[INDUPD] >= 256) {
          Pupdatethreshold[INDUPD] = 255;
        }

        if (Pupdatethreshold[INDUPD] < 0) {
          Pupdatethreshold[INDUPD] = 0;
        }

        ctrupdate(Bias[INDBIAS], e.taken, PERCWIDTH);
        ctrupdate(BiasSK[INDBIASSK], e.taken, PERCWIDTH);
#ifdef IMLI
#ifndef IMLISIC
        long long interIMLIcount = IMLIcount;
        /* just a trick to disable IMLIcount*/
        IMLIcount = 0;
#endif
#endif
        Gupdate<LOGGNB, GNB>(e.taken, GGEHL);
        Gupdate<LOGLNB, LNB>(e.taken, LGEHL);

        Gupdate<LOGPNB, PNB>(e.taken, PGEHL);

#ifdef IMLI
#ifdef IMLISIC
        Gupdate<LOGINB, INB>(e.taken, IGEHL);
#else
        IMLIcount = interIMLIcount;
#endif

#ifdef IMLIOH
        if (IMLIcount >= 2) {
          Gupdate<LOGFNB, FNB>(e.taken, FGEHL);
        }
#endif

#endif
      }
      // ends update of the SC states
    }

    // TAGE UPDATE
    if (true) {
      bool ALLOC = ((tage_pred != e.taken) & (HitBank < nhist));
      if (pred_taken == e.taken) {
        if ((MYRANDOM() & 31) != 0) {
          ALLOC = false;
        }
      }

      if (HitBank > 0) {
        // Manage the selection between longest matching and alternate matching
        // for "pseudo"-newly allocated longest matching entry
        bool PseudoNewAlloc = get_gentry(HitBank).ctr_weak();
        // an entry is considered as newly allocated if its prediction counter is weak
        if (PseudoNewAlloc) {
          if (LongestMatchPred == e.taken) {
            ALLOC = false;
          }

          // if it was delivering the correct prediction, no need to allocate a new entry
          // even if the overall prediction was false
#ifndef POSTPREDICT
          // FIXME: Have a PC (or T1 history) based use_alt table
          if (LongestMatchPred != alttaken) {
            int index = (INDUSEALT) ^ LongestMatchPred;
            ctrupdate(use_alt_on_na[index][HitBank > (nhist / 3)], (alttaken == e.taken), 4);
          }
#endif
        }
      }

      bool noAlloc = e.no_alloc;
      ALLOC        = ALLOC & noAlloc;  // flag to alloc and noAlloc

      if (ALLOC) {
        int T = 1;  // nhist; // Seznec has 1

        int A = 1;
        if ((MYRANDOM() & 127) < 32 && nhist > 8) {
          A = 2;
        }

        int Penalty = 0;
        int NA      = 0;

        int weakBank = HitBank + A;

        // Allocate a new entry in a longer-history bank
        if (T > 0) {
          weakBank = HitBank + A;
          for (int i = weakBank; i <= nhist; i += 1) {
            if (get_gentry(i).u_get() == 0) {
              weakBank = i;

              get_gentry(i).reset(GTAG[i], e.taken);

              NA++;

              if (T <= 0) {
                break;
              }

              i += 1;
              T -= 1;
            } else {
              Penalty++;
            }
          }
        }
        // Could not find a place to allocate

        TICK += (Penalty - NA);
        if (TICK < -127) {
          TICK = -127;
        } else if (TICK > 63) {
          TICK = 63;
        }
        if (T) {
          if (TICK > 0) {
            for (int i = HitBank + 1; i <= nhist; i += 1) {
              int idx1 = get_tage_pos(i);

              gtable[i][idx1].u_dec();
              TICK--;
              // It two banks are available
              // int idx2 = idx1 ^ 0x1; // Toggle bank selection bit
              // gtable[i][idx2].u_dec();
              // TICK--;
            }
          }
        }
      }
      // TODO: recheck that this is better
      if (HitBank) {
        if (get_gentry(HitBank).isHit()) {
          if (LongestMatchPred != e.taken) {
            get_gentry(HitBank).u_dec();
          }
        }
      }

      if (HitBank > 0) {
        get_gentry(HitBank).ctr_update(e.taken);
        if (get_gentry(HitBank).u_get() == 0 && AltBank > 0) {
          get_gentry(AltBank).ctr_update(e.taken);
        } else {
          bimodal.update(e.taken);
        }
        if (LongestMatchPred != alttaken) {   // HitBank and AltBank dissagree
          if (LongestMatchPred == e.taken) {  // LongestMatchPred == resolveDir && !noAlloc
            get_gentry(HitBank).u_inc();
          } else {
            get_gentry(HitBank).u_dec();
          }
        }
      } else {
        bimodal.update(e.taken);
      }
    }
#ifdef POSTPREDICT
    I(ppi < postpsize);
    ctrupdate(postp[ppi], e.taken, POSTPBITS);
#endif
    // END TAGE UPDATE

        bim_tag_offset++;
    if (state.advance_imli_tag_offset) {
      imli_tag_offset++;
    }
  }

  template <std::size_t S1, std::size_t S2>
  int Gpredict(const std::array<std::array<int8_t, 1 << S1>, S2>& tab) {
    int       PERCSUM = 0;
    const int NBR     = tab.size();
    // const int logs    = tab[0].size();
    for (int i = 0; i < NBR; i++) {
      // long long bhist = BHIST & ((long long)((1 << length[i]) - 1));
      int16_t ctr = tab[i][GI[i]];
      PERCSUM += (2 * ctr + 1);
    }

    return PERCSUM;
  }

  template <std::size_t S1, std::size_t S2>
  void Gupdate(bool taken, std::array<std::array<int8_t, (1 << S1)>, S2>& tab) {
    const int NBR = tab.size();
    // const int logs = tab[0].size();
    for (int i = 0; i < NBR; i++) {
      // long long bhist = BHIST & ((long long)((1 << length[i]) - 1));
      ctrupdate(tab[i][GI[i]], taken, PERCWIDTH - (i < (NBR - 1)));
    }
  }

  void TrackOtherInst(Addr_t orig_PC, Opcode opType, Addr_t branchTarget) {
    bool taken = true;

    // bim_tag_offset++;
    // imli_tag_offset++;
    deferred_ops.push_back({
        .has_predictor_update = false,
        .orig_pc              = orig_PC,
        .brtype               = opType,
        .taken                = taken,
        .target               = branchTarget,
        .no_alloc             = false,
        .state                = {},
    });
  }
};
