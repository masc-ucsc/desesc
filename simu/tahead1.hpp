// Developped by A. Seznec
// This simulator corresponds to the slide set "TAGE: an engineering cookbook by Andre Seznec, November 2024"

// it was developped usiung the framework used for CBP 2016
// file main.cc was  modified to stop the simulation after 10000000 branches for all benchmarks and to potentially accomodate the
// option WARM
#ifndef _TAHEAD1_H
#define _TAHEAD1_H

#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "opcode.hpp"
// #include "utils.h"
// #include "bt9.h"
// #include "bt9_reader.h"

// if one wants to test with "more realistic" initial states
// #define WARM // 5000000 branches to warm the predictor if one wants a warmed predictor
// #define TAHEAD1_RANDINIT  // TAHEAD1_RANDINIT provide random values in all counters, might be slightly more realistic than
// initialization with weak counters

// Possible conf option if updates are delayed to end of fetch_boundary (BPred.cpp:pending)
// #define TAHEAD1_DELAY_UPDATE 1

#define TAHEAD1_LOGSCALE 4
#define TAHEAD1_LOGT     (6 + TAHEAD1_LOGSCALE)  /* logsize of a logical  TAGE tables */
#define TAHEAD1_LOGB     (6 + TAHEAD1_LOGSCALE)  // log of number of entries in bimodal predictor
#define TAHEAD1_LOGBIAS  (6 + TAHEAD1_LOGSCALE)  // logsize of tables in TAHEAD1_SC

#if (TAHEAD1_LOGSCALE == 4)
#define TAHEAD1_MINHIST 2
#define TAHEAD1_MAXHIST 350
#endif
#if (TAHEAD1_LOGSCALE == 3)
#define TAHEAD1_MINHIST 2
#define TAHEAD1_MAXHIST 250
#endif
#if (TAHEAD1_LOGSCALE == 2)
#define TAHEAD1_MINHIST 2
#define TAHEAD1_MAXHIST 250
#endif
#if (TAHEAD1_LOGSCALE == 1)
#define TAHEAD1_MINHIST 2
#define TAHEAD1_MAXHIST 250
#endif

#if (TAHEAD1_LOGSCALE == 0)
#define TAHEAD1_MINHIST 1
#define TAHEAD1_MAXHIST 250
#endif

#define TAHEAD1_MAXBR 8  // Maximum TAHEAD1_MAXBR  branches in  the block; the code assumes TAHEAD1_MAXBR is a power of 2
#define TAHEAD1_NBREADPERTABLE 4   // predictions read per table for a block

#define TAHEAD1_AHEAD 0
// in the curent version:  only 0 or 2 are valid (0 corresponds to the conventional 1-block ahead, 2 coresponds to the 3-block
// ahead)

// FIXME: DISSABLING code
// general prameters
// Only useful when TAHEAD1_AHEAD==2
#define TAHEAD1_READWIDTHAHEAD 16  // the number of entries read in each tagged table   (per way if associative),
#define TAHEAD1_TAGCHECKAHEAD  4   // the number of tag checks per entries,
//  (16,4) and (8,8) seems good design points

#define TAHEAD1_NHIST 6  // 14  different history lengths, but 7 physical tables

#define TAHEAD1_UWIDTH 2
#define TAHEAD1_LOGASSOC \
  1  // associative tagged tables are probably  not worth the effort at TAHEAD1_TBITS=12 : about 0.02 MPKI gain for associativity 2;
     // an extra tag bit would be  needed to get some gain with associativity 4 // but partial skewed associativity (option
     // TAHEAD1_PSK) might be interesting
#define TAHEAD1_TBITS 12  // if 11 bits: benefit from associativity vanishes

#define TAHEAD1_LOGG  (TAHEAD1_LOGT - TAHEAD1_LOGASSOC)  // size of way in a logical TAGE table
#define TAHEAD1_ASSOC (1 << TAHEAD1_LOGASSOC)

#define TAHEAD1_HYSTSHIFT 1  // bimodal hysteresis shared among (1<< TAHEAD1_HYSTSHIFT) entries
#define TAHEAD1_BIMWIDTH  3  //  with of the counter in the bimodal predictor
// A. Seznec: I just played using 3-bit counters in the simulator, using 2-bit counters but TAHEAD1_HYSTSHIFT=0 brings similar
// accuracy

/////////////////////////////////////////////
// Options  for optimizations of TAGE
// #define TAHEAD1_INTERLEAVED // just to show that it  is not  fully interleaving the banks is not that great and probably not
// worth the extra 14x14 shuffling/reshuffling
#ifdef TAHEAD1_INTERLEAVED
#define TAHEAD1_SHARED        0
#define TAHEAD1_ADJACENTTABLE 1
int BANK1;
#endif

/////////////////////////////////////////////////
// the replacement/allocation policies described in the slide set
//#define TAHEAD1_OPTTAGE
#ifdef TAHEAD1_OPTTAGE
#ifndef TAHEAD1_INTERLEAVED
#define TAHEAD1_ADJACENTTABLE \
  1  // ~+0.076,  if 14 tables :7 physical tables: Logical table T(2i-1) and T(2i) are mapped on the the same physical P(i), but the
     // two predictions are adjacent and  are read with index computed with H(2i-1), the tags are respectively computed with  for
     // H(2i-1) and H(2i).
#define TAHEAD1_SHARED 1  // (T1/T9) (T2/T10)   shared the same bank T9 and T10 do not share with anybody: ~ -0.076 MPKI
#endif
#define TAHEAD1_OPTGEOHIST  // we can do better than geometric series
// Optimizations  allocation/replacement: globally; ~0.09
#define TAHEAD1_FILTERALLOCATION 1  // ~ -0.04 MPKI
#define TAHEAD1_FORCEU           1  // don't work if only one U  bit	// from times selective allocation with u = 1: ~0.015 MPKI

#if (TAHEAD1_LOGASSOC == 1)
// A. Seznec: partial skewed associativity, remmeber that I invented it in 1993 :-)
#define TAHEAD1_PSK 1
#define TAHEAD1_REPSK \
  1  // this optimization is funny, if no "useless" entry, move the entry on the other way to make room, brings a little bit of
     // accuracy
#else
#define TAHEAD1_PSK   1
#define TAHEAD1_REPSK 0
#endif

#define TAHEAD1_PROTECTRECENTALLOCUSEFUL 1  // Recently allocated entries  are protected against the smart u reset: ~ 0.007 MPKI
#define TAHEAD1_UPDATEALTONWEAKMISP \
  1  // When the Longest match is weak and wrong, one updates also the alternate prediction and HCPred : ~0.018 MPKI

#else
#define TAHEAD1_SHARED 0
#define TAHEAD1_PSK    0
#define TAHEAD1_REPSK  0
#endif
//////////////////////////////////////////////

/////////////////////////////////////////////
/// For the TAHEAD1_SC component
#define TAHEAD1_SC  // Enables the statistical corrector
#ifndef TAHEAD1_SC
#define LMP  // systematically use TAHEAD1_LongestMatchPred, but with an optimized allocation policy.
// In practice the optimizations on TAGE brings significant gains
#endif

#define TAHEAD1_FORCEONHIGHCONF  //   if TAGE is high conf and TAHEAD1_SC very low conf then use TAGE, if TAHEAD1_SC: brings 0.008 -
                                 //   0.016 MPKI, but a
                                 //   5-to-1 mux instead a 4-to-1
// #define TAHEAD1_MORESCLOGICAHEAD // if TAHEAD1_AHEAD and if TAHEAD1_SC uses four times  the number of adder trees (compute 16
// SCsum  per prediction !), ~ 1 % gain in accuracy

// Add the extra TAHEAD1_SC tables
#define TAHEAD1_SCMEDIUM
#ifdef TAHEAD1_SCMEDIUM
#define TAHEAD1_SCFULL
// 4 tables for IMLI and global history variation: see slide set
#endif

#define TAHEAD1_PERCWIDTH 6  // Statistical corrector counter width: if FULL  6 bits brings 0.007
/////////////////////////////////////////////////

int TAHEAD1_NPRED = 20;  // this variable needs to be larger than TAHEAD1_AHEAD to avoid core dump when TAHEAD1_AHEAD prediction
// I was wanting to test large TAHEAD1_AHEAD distances up to 9
uint     TAHEAD1_AHGI[10][TAHEAD1_NHIST + 1];    // indexes to the different tables are computed only once
uint     TAHEAD1_AHGTAG[10][TAHEAD1_NHIST + 1];  // tags for the different tables are computed only once
uint64_t TAHEAD1_Numero;                         // Number of the branch in the basic block
uint64_t TAHEAD1_PCBLOCK;
uint64_t TAHEAD1_PrevPCBLOCK;
uint64_t TAHEAD1_PrevNumero;

// To get the predictor storage budget on stderr  uncomment the next line
#define TAHEAD1_PRINTSIZE
#include <vector>

//////////////////////////////////
////////The statistical corrector components

// The base table  in the TAHEAD1_SC component indexed with only PC + information flowing out from  TAGE
//  In order to  allow computing SCSUM in parallel with TAGE check, only TAHEAD1_LongestMatchPred and TAHEAD1_HCpred are used. 4
//  SCSUM are computed, and a final 4-to-1 selects the correct prediction:   each extra bit of information (confidence, etc) would
//  necessitate  doubling the number of computed SCSUMs and double the width of the final MUX

// if only PC-based TAHEAD1_SC these ones are useful
int8_t TAHEAD1_BiasGEN;
int8_t TAHEAD1_BiasAP[2];
int8_t TAHEAD1_BiasLM[2];
//////

int8_t TAHEAD1_BiasLMAP[4];
int8_t TAHEAD1_BiasPC[1 << TAHEAD1_LOGBIAS];
int8_t TAHEAD1_BiasPCLMAP[(1 << TAHEAD1_LOGBIAS)];

#define TAHEAD1_LOGINB TAHEAD1_LOGBIAS
int    TAHEAD1_Im = TAHEAD1_LOGBIAS;
int8_t TAHEAD1_IBIAS[(1 << TAHEAD1_LOGINB)];
int8_t TAHEAD1_IIBIAS[(1 << TAHEAD1_LOGINB)];

// Back path history; (in practice  when a  new backward branch is  reached; 2 bits are pushed in the history
#define TAHEAD1_LOGBNB TAHEAD1_LOGBIAS
int    TAHEAD1_Bm = TAHEAD1_LOGBIAS;
int8_t TAHEAD1_BBIAS[(1 << TAHEAD1_LOGBNB)];
//////////////// Forward path history (taken)
#define TAHEAD1_LOGFNB TAHEAD1_LOGBIAS
int    TAHEAD1_Fm = TAHEAD1_LOGBIAS;
int8_t TAHEAD1_FBIAS[(1 << TAHEAD1_LOGFNB)];

// indices for the  TAHEAD1_SC tables
#define TAHEAD1_INDBIASLMAP (TAHEAD1_LongestMatchPred + (TAHEAD1_HCpred << 1))
#define TAHEAD1_PSNUM \
  ((((TAHEAD1_AHEAD) ? ((TAHEAD1_Numero ^ TAHEAD1_PCBLOCK) & (TAHEAD1_MAXBR - 1)) : (TAHEAD1_Numero & (TAHEAD1_MAXBR - 1)))) << 2)

#ifdef TAHEAD1_MORESCLOGICAHEAD
#define TAHEAD1_PCBL ((TAHEAD1_AHEAD) ? (TAHEAD1_PrevPCBLOCK ^ ((TAHEAD1_GH)&3)) : (TAHEAD1_PCBLOCK))
#else
#define TAHEAD1_PCBL ((TAHEAD1_AHEAD) ? (TAHEAD1_PrevPCBLOCK) : (TAHEAD1_PCBLOCK))
#endif

#define TAHEAD1_INDBIASPC \
  (((((TAHEAD1_PCBL ^ (TAHEAD1_PCBL >> (TAHEAD1_LOGBIAS - 5))))) & ((1 << TAHEAD1_LOGBIAS) - 1)) ^ TAHEAD1_PSNUM)
#define TAHEAD1_INDBIASPCLMAP (TAHEAD1_INDBIASPC) ^ ((TAHEAD1_LongestMatchPred ^ (TAHEAD1_HCpred << 1)) << (TAHEAD1_LOGBIAS - 2))
// a single  physical table but  two logic tables: indices agree on all the bits except 2

#define TAHEAD1_INDBIASBHIST \
  (((((TAHEAD1_PCBL ^ TAHEAD1_PrevBHIST ^ (TAHEAD1_PCBL >> (TAHEAD1_LOGBIAS - 4))))) & ((1 << TAHEAD1_LOGBNB) - 1)) ^ TAHEAD1_PSNUM)
#define TAHEAD1_INDBIASFHIST \
  (((((TAHEAD1_PCBL ^ TAHEAD1_PrevFHIST ^ (TAHEAD1_PCBL >> (TAHEAD1_LOGBIAS - 3))))) & ((1 << TAHEAD1_LOGFNB) - 1)) ^ TAHEAD1_PSNUM)
#define TAHEAD1_INDBIASIMLIBR                                                                                          \
  (((((TAHEAD1_PCBL ^ TAHEAD1_PrevF_BrIMLI ^ (TAHEAD1_PCBL >> (TAHEAD1_LOGBIAS - 6))))) & ((1 << TAHEAD1_LOGINB) - 1)) \
   ^ TAHEAD1_PSNUM)
#define TAHEAD1_INDBIASIMLITA                                                                                                 \
  ((((((TAHEAD1_PCBL >> 4) ^ TAHEAD1_PrevF_TaIMLI ^ (TAHEAD1_PCBL << (TAHEAD1_LOGBIAS - 4))))) & ((1 << TAHEAD1_LOGINB) - 1)) \
   ^ TAHEAD1_PSNUM)

//////////////////////IMLI RELATED and backward/Forward history////////////////////////////////////
long long TAHEAD1_TaIMLI;    // use to monitor the iteration number (based on target locality for backward branches)
long long TAHEAD1_BrIMLI;    // use to monitor the iteration number (a second version based on backward branch locality))
long long TAHEAD1_F_TaIMLI;  // use to monitor the iteration number,TAHEAD1_BHIST if TAHEAD1_TaIMLI = 0
long long TAHEAD1_F_BrIMLI;  // use to monitor the iteration number (a second version), TAHEAD1_FHIST if TAHEAD1_BrIMLI = 0
long long TAHEAD1_BHIST;
long long TAHEAD1_FHIST;

// Same thing but a cycle TAHEAD1_AHEAD
long long TAHEAD1_PrevF_TaIMLI;  // use to monitor the iteration number, TAHEAD1_BHIST if TAHEAD1_TaIMLI = 0
long long TAHEAD1_PrevF_BrIMLI;  // use to monitor the iteration number (a second version), TAHEAD1_FHIST if TAHEAD1_BrIMLI = 0
long long TAHEAD1_PrevBHIST;
long long TAHEAD1_PrevFHIST;

// Needs for computing the "histories" for IMLI and backward/forward histories
uint64_t TAHEAD1_LastBack;
uint64_t TAHEAD1_LastBackPC;
uint64_t TAHEAD1_BBHIST;

// update threshold for the statistical corrector
#define TAHEAD1_WIDTHRES 8
int TAHEAD1_updatethreshold;

int TAHEAD1_SUMSC;
int TAHEAD1_SUMFULL;

bool TAHEAD1_predTSC;
bool TAHEAD1_predSC;
bool TAHEAD1_pred_inter;

////  FOR TAGE //////

#define TAHEAD1_HISTBUFFERLENGTH 4096  // we use a 4K entries history buffer to store the branch history

#define TAHEAD1_BORNTICK 4096
// for the allocation policy

// utility class for index computation
// this is the cyclic shift register for folding
// a long global history into a smaller number of bits; see P. Michaud's PPM-like predictor at CBP-1

class TAHEAD1_folded_history {
public:
  unsigned comp;
  int      CLENGTH;
  int      OLENGTH;
  int      OUTPOINT;
  int      INTEROUT;

  TAHEAD1_folded_history() {}

  void init(int original_length, int compressed_length, int N) {
    (void)N;
    comp     = 0;
    OLENGTH  = original_length;
    CLENGTH  = compressed_length;
    OUTPOINT = OLENGTH % CLENGTH;
  }

  void update(uint8_t *h, int PT) {
    comp = (comp << 1) ^ h[PT & (TAHEAD1_HISTBUFFERLENGTH - 1)];

    comp ^= h[(PT + OLENGTH) & (TAHEAD1_HISTBUFFERLENGTH - 1)] << OUTPOINT;
    comp ^= (comp >> CLENGTH);
    comp = (comp) & ((1 << CLENGTH) - 1);
  }
};

class TAHEAD1_bentry  // TAGE bimodal table entry
{
public:
  int8_t hyst;
  int8_t pred;
  TAHEAD1_bentry() {
    pred = 0;
    hyst = 1;
  }
};

class TAHEAD1_gentry  // TAGE global table entry
{
public:
  int8_t ctr;
  uint   tag;
  int8_t u;

  TAHEAD1_gentry() {
    ctr = 0;
    u   = 0;
    tag = 0;
  }
};

bool TAHEAD1_alttaken;  // alternate   TAGE prediction if the longest match was not hitting: needed for updating the u bit
bool TAHEAD1_HCpred;    // longest not low confident match or base prediction if no confident match

bool   TAHEAD1_tage_pred;  // TAGE prediction
bool   TAHEAD1_LongestMatchPred;
int    TAHEAD1_HitBank;     // longest matching bank
int    TAHEAD1_AltBank;     // alternate matching bank
int    TAHEAD1_HCpredBank;  // longest non weak  matching bank
int    TAHEAD1_HitAssoc;
int    TAHEAD1_AltAssoc;
int    TAHEAD1_HCpredAssoc;
int    TAHEAD1_Seed;  // for the pseudo-random number generator
int8_t TAHEAD1_BIM;   // the bimodal prediction

int8_t TAHEAD1_CountMiss11  = -64;  // more or less than 11% of misspredictions
int8_t TAHEAD1_CountLowConf = 0;

int8_t TAHEAD1_COUNT50[TAHEAD1_NHIST + 1];     // more or less than 50%  misprediction on weak TAHEAD1_LongestMatchPred
int8_t TAHEAD1_COUNT16_31[TAHEAD1_NHIST + 1];  // more or less than 16/31th  misprediction on weak TAHEAD1_LongestMatchPred
int    TAHEAD1_TAGECONF;                       // TAGE confidence  from 0 (weak counter) to 3 (saturated)

#define TAHEAD1_PHISTWIDTH 27  // width of the path history used in TAGE
#define TAHEAD1_CWIDTH     3   // predictor counter width on the TAGE tagged tables

// the counter(s) to chose between longest match and alternate prediction on TAGE when weak counters: only plain TAGE
#define TAHEAD1_ALTWIDTH 5
int8_t TAHEAD1_use_alt_on_na;
int    TAHEAD1_TICK, TAHEAD1_TICKH;  // for the reset of the u counter

uint8_t TAHEAD1_ghist[TAHEAD1_HISTBUFFERLENGTH];
int     TAHEAD1_ptghist;
// for managing global path history

long long              TAHEAD1_phist;                       // path history
int                    TAHEAD1_GH;                          //  another form of path history
TAHEAD1_folded_history tahead1_ch_i[TAHEAD1_NHIST + 1];     // utility for computing TAGE indices
TAHEAD1_folded_history TAHEAD1_ch_t[2][TAHEAD1_NHIST + 1];  // utility for computing TAGE tags

// For the TAGE predictor
TAHEAD1_bentry *TAHEAD1_btable;                     // bimodal TAGE table
TAHEAD1_gentry *TAHEAD1_gtable[TAHEAD1_NHIST + 1];  // tagged TAGE tables
int             TAHEAD1_m[TAHEAD1_NHIST + 1];
uint            TAHEAD1_GI[TAHEAD1_NHIST + 1];                  // indexes to the different tables are computed only once
uint            TAHEAD1_GGI[TAHEAD1_ASSOC][TAHEAD1_NHIST + 1];  // indexes to the different tables are computed only once
uint            TAHEAD1_GTAG[TAHEAD1_NHIST + 1];                // tags for the different tables are computed only once
int             TAHEAD1_BI;                                     // index of the bimodal table
bool            TAHEAD1_pred_taken;                             // prediction

int TAHEAD1_incval(int8_t ctr) {
  return (2 * ctr + 1);
  // to center the sum
  //  probably not worth, but don't understand why
}

int TAHEAD1_predictorsize() {
  int STORAGESIZE = 0;
  int inter       = 0;

  STORAGESIZE += TAHEAD1_NHIST * (1 << TAHEAD1_LOGG) * (TAHEAD1_CWIDTH + TAHEAD1_UWIDTH + TAHEAD1_TBITS) * TAHEAD1_ASSOC;
#ifndef TAHEAD1_SC
  STORAGESIZE += TAHEAD1_ALTWIDTH;
  // the use_alt counter
#endif
  STORAGESIZE += (1 << TAHEAD1_LOGB) + (TAHEAD1_BIMWIDTH - 1) * (1 << (TAHEAD1_LOGB - TAHEAD1_HYSTSHIFT));
  STORAGESIZE += TAHEAD1_m[TAHEAD1_NHIST];     // the history bits
  STORAGESIZE += TAHEAD1_PHISTWIDTH;           // TAHEAD1_phist
  STORAGESIZE += 12;                           // the TAHEAD1_TICK counter
  STORAGESIZE += 12;                           // the TAHEAD1_TICKH counter
  STORAGESIZE += 2 * 7 * (TAHEAD1_NHIST / 4);  // counters TAHEAD1_COUNT50 TAHEAD1_COUNT16_31
  STORAGESIZE += 8;                            // TAHEAD1_CountMiss11
  STORAGESIZE += 36;                           // for the random number generator
  fprintf(stderr, " (TAGE %d) ", STORAGESIZE);
#ifdef TAHEAD1_SC

  inter += TAHEAD1_WIDTHRES;
  inter += (TAHEAD1_PERCWIDTH)*2 * (1 << TAHEAD1_LOGBIAS);  // TAHEAD1_BiasPC and TAHEAD1_BiasPCLMAP,
  inter += (TAHEAD1_PERCWIDTH)*2;                           // TAHEAD1_BiasLMAP

#ifdef TAHEAD1_SCMEDIUM
#ifdef TAHEAD1_SCFULL

  inter += (1 << TAHEAD1_LOGFNB) * TAHEAD1_PERCWIDTH;
  inter += TAHEAD1_LOGFNB;
  inter += (1 << TAHEAD1_LOGBNB) * TAHEAD1_PERCWIDTH;
  inter += TAHEAD1_LOGBNB;
  inter += (1 << TAHEAD1_LOGINB) * TAHEAD1_PERCWIDTH;  // two forms
  inter += TAHEAD1_LOGBIAS;
  inter += 10;  // TAHEAD1_LastBackPC
#endif
  inter += (1 << TAHEAD1_LOGINB) * TAHEAD1_PERCWIDTH;  // two forms
  inter += TAHEAD1_LOGBIAS;
  inter += 10;  // TAHEAD1_LastBack
#endif

  STORAGESIZE += inter;

  fprintf(stderr, " (TAHEAD1_SC %d) ", inter);
#endif
#ifdef TAHEAD1_PRINTSIZE

  fprintf(stderr, " (TOTAL %d, %d Kbits)\n  ", STORAGESIZE, STORAGESIZE / 1024);
  fprintf(stdout, " (TOTAL %d %d Kbits)\n  ", STORAGESIZE, STORAGESIZE / 1024);
#endif

  return (STORAGESIZE);
}

class Tahead1 {
public:
  Tahead1(void) {
    reinit();
#ifdef TAHEAD1_PRINTSIZE
    TAHEAD1_predictorsize();
#endif
  }

#define TAHEAD1_NNHIST 10
  int mm[TAHEAD1_NNHIST + 1];

  int TAHEAD1_getTableSize(int i) {
    if (TAHEAD1_SHARED && i >= 1 && i <= 6) {
      return (1 << (TAHEAD1_LOGG + 1)) * TAHEAD1_ASSOC;
    } else {
      return (1 << TAHEAD1_LOGG) * TAHEAD1_ASSOC;
    }
  }

  TAHEAD1_gentry &get_TAHEAD1_gtable_entry(int i, int j) {
    int idx = j % TAHEAD1_getTableSize(i);
    return TAHEAD1_gtable[i][idx];
  }

  TAHEAD1_bentry &get_TAHEAD1_btable_entry(int j) {
    int idx = j % (1 << TAHEAD1_LOGB);
    return TAHEAD1_btable[idx];
  }

  void reinit() {
    if ((TAHEAD1_AHEAD != 0) && (TAHEAD1_AHEAD != 2)) {
      printf("Sorry the simulator does not support this TAHEAD1_AHEAD distance\n");
      exit(1);
    }
    if ((TAHEAD1_LOGASSOC != 1) || (TAHEAD1_PSK == 0)) {
#if (TAHEAD1_REPSK == 1)

      printf("Sorry TAHEAD1_REPSK only with associativity 2 and TAHEAD1_PSK activated\n");
      exit(1);

#endif
    }

#ifdef TAHEAD1_OPTGEOHIST
    mm[1] = TAHEAD1_MINHIST;

    for (int i = 2; i <= TAHEAD1_NNHIST; i++) {
      mm[i] = (int)(((double)TAHEAD1_MINHIST
                     * pow((double)(TAHEAD1_MAXHIST) / (double)TAHEAD1_MINHIST, (double)(i - 1) / (double)((TAHEAD1_NNHIST - 1))))
                    + 0.5);
    }
    for (int i = 2; i <= TAHEAD1_NNHIST; i++) {
      if (mm[i] <= mm[i - 1] + 1) {
        mm[i] = mm[i - 1] + 1;
      }
    }
    int PT = 1;
    for (int i = 1; i <= 3; i += 2) {
      TAHEAD1_m[PT] = mm[i];
      PT++;
    }

    for (int i = 5; i <= TAHEAD1_NHIST; i++)

    {
      TAHEAD1_m[PT] = mm[i];
      PT++;
    }
    PT = TAHEAD1_NHIST;

    for (int i = TAHEAD1_NNHIST; i > TAHEAD1_NHIST; i -= 2) {
      TAHEAD1_m[PT] = mm[i];
      PT--;
    }

#else
    TAHEAD1_m[1] = TAHEAD1_MINHIST;

    for (int i = 2; i <= TAHEAD1_NHIST; i++) {
      TAHEAD1_m[i]
          = (int)(((double)TAHEAD1_MINHIST
                   * pow((double)(TAHEAD1_MAXHIST) / (double)TAHEAD1_MINHIST, (double)(i - 1) / (double)((TAHEAD1_NHIST - 1))))
                  + 0.5);
    }
    for (int i = 3; i <= TAHEAD1_NHIST; i++) {
      if (TAHEAD1_m[i] <= TAHEAD1_m[i - 1]) {
        TAHEAD1_m[i] = TAHEAD1_m[i - 1] + 1;
      }
    }
#endif
    if ((TAHEAD1_AHEAD != 0) & (TAHEAD1_AHEAD != 2)) {
      exit(1);  // prediction is considered to be done in 1 cycle or 3 cycles
    }
    for (int i = 1; i <= TAHEAD1_NHIST; i++) {
      TAHEAD1_m[i] -= TAHEAD1_AHEAD;
    }

#ifdef TAHEAD1_ADJACENTTABLE
    if (TAHEAD1_LOGASSOC == 0) {
      //  if there is some associativity: no need for this
      for (int i = 2; i <= TAHEAD1_NHIST; i += 2)

      {
        TAHEAD1_m[i] = TAHEAD1_m[i - 1] + ((TAHEAD1_m[i] - TAHEAD1_m[i - 1]) / 2);

        if (TAHEAD1_m[i] == TAHEAD1_m[i - 1]) {
          TAHEAD1_m[i]++;
        }
      }
    }

#endif
    for (int i = 1; i <= TAHEAD1_NHIST; i++) {
      TAHEAD1_m[i] <<= 2;
    }
    // 4 bits per block

    for (int i = 1; i <= TAHEAD1_NHIST; i++) {
      printf("%d ", TAHEAD1_m[i]);
    }
    printf("\n");
    //printf ("TAHEAD1_m[TAHEAD1_NHIST] = %u\n", TAHEAD1_m[TAHEAD1_NHIST]);
#ifndef TAHEAD1_INTERLEAVED
    if (TAHEAD1_SHARED) {
      /* tailored for 14 tables */
      for (int i = 1; i < TAHEAD1_NNHIST/2; i++) {
        TAHEAD1_gtable[i] = new TAHEAD1_gentry[(1 << (TAHEAD1_LOGG + (i <= 6))) * TAHEAD1_ASSOC];
      }
      for (int i = TAHEAD1_NNHIST/2; i <= TAHEAD1_NHIST; i++) {
        TAHEAD1_gtable[i] = TAHEAD1_gtable[i - 8];
      }
    }

    else {
      for (int i = 1; i <= TAHEAD1_NHIST; i++) {
        TAHEAD1_gtable[i] = new TAHEAD1_gentry[(1 << (TAHEAD1_LOGG)) * TAHEAD1_ASSOC];
      }
    }
#else
    TAHEAD1_gtable[1] = new TAHEAD1_gentry[(1 << (TAHEAD1_LOGG)) * TAHEAD1_ASSOC * TAHEAD1_NHIST];

    for (int i = 2; i <= TAHEAD1_NHIST; i++) {
      TAHEAD1_gtable[i] = TAHEAD1_gtable[1];
    }

#endif

    TAHEAD1_btable = new TAHEAD1_bentry[1 << TAHEAD1_LOGB];
    for (int i = 1; i <= TAHEAD1_NHIST; i++) {
      tahead1_ch_i[i].init(TAHEAD1_m[i], 25 + (2 * ((i - 1) / 2) % 4), i - 1);
      TAHEAD1_ch_t[0][i].init(tahead1_ch_i[i].OLENGTH, 13, i);
      TAHEAD1_ch_t[1][i].init(tahead1_ch_i[i].OLENGTH, 11, i + 2);
    }

    TAHEAD1_Seed = 0;

    TAHEAD1_TICK  = 0;
    TAHEAD1_phist = 0;
    TAHEAD1_Seed  = 0;

    for (int i = 0; i < TAHEAD1_HISTBUFFERLENGTH; i++) {
      TAHEAD1_ghist[0] = 0;
    }
    TAHEAD1_ptghist = 0;

    TAHEAD1_updatethreshold = 23;

#ifdef TAHEAD1_SCMEDIUM

    for (int j = 0; j < ((1 << TAHEAD1_LOGBNB) - 1); j++) {
      if (!(j & 1)) {
        TAHEAD1_BBIAS[j] = -1;
      }
    }
    for (int j = 0; j < ((1 << TAHEAD1_LOGFNB) - 1); j++) {
      if (!(j & 1)) {
        TAHEAD1_FBIAS[j] = -1;
      }
    }
    for (int j = 0; j < ((1 << TAHEAD1_LOGINB) - 1); j++) {
      if (!(j & 1)) {
        TAHEAD1_IBIAS[j] = -1;
      }
    }
    for (int j = 0; j < ((1 << TAHEAD1_LOGINB) - 1); j++) {
      if (!(j & 1)) {
        TAHEAD1_IIBIAS[j] = -1;
      }
    }

#endif

    for (int j = 0; j < (1 << TAHEAD1_LOGBIAS); j++) {
      switch (j & 3) {
        case 0: TAHEAD1_BiasPCLMAP[j] = -8; break;
        case 1: TAHEAD1_BiasPCLMAP[j] = 7; break;
        case 2: TAHEAD1_BiasPCLMAP[j] = -32; break;
        case 3: TAHEAD1_BiasPCLMAP[j] = 31; break;
      }
    }

    TAHEAD1_TICK = 0;

    TAHEAD1_ptghist = 0;
    TAHEAD1_phist   = 0;
#ifdef TAHEAD1_RANDINIT
    if (TAHEAD1_NHIST == 14) {
      for (int i = 1; i <= ((TAHEAD1_SHARED) ? 8 : 14); i++) {
        for (int j = 0; j < TAHEAD1_ASSOC * (1 << (TAHEAD1_LOGG + (TAHEAD1_SHARED ? (i <= 6) : 0))); j++) {
          int idx                            = j % TAHEAD1_getTableSize(i);
          get_TAHEAD1_gtable_entry(i, idx).u = random() & ((1 << TAHEAD1_UWIDTH) - 1);

          get_TAHEAD1_gtable_entry(i, idx).ctr = (random() & 7) - 4;
        }
      }
    }

    else {
      for (int i = 1; i <= TAHEAD1_NHIST; i++) {
        for (int j = 0; j < TAHEAD1_ASSOC * (1 << TAHEAD1_LOGG); j++) {
          int idx                              = j % TAHEAD1_getTableSize(i);
          get_TAHEAD1_gtable_entry(i, idx).u   = random() & ((1 << TAHEAD1_UWIDTH) - 1);
          get_TAHEAD1_gtable_entry(i, idx).ctr = (random() & 7) - 4;
        }
      }
    }

    TAHEAD1_TICK  = TAHEAD1_BORNTICK / 2;
    TAHEAD1_TICKH = TAHEAD1_BORNTICK / 2;
    for (int i = 0; i < (1 << TAHEAD1_LOGB); i++) {
      get_TAHEAD1_btable_entry(i).pred = random() & 1;
      get_TAHEAD1_btable_entry(i).hyst = random() & 3;
    }
    TAHEAD1_updatethreshold = 23;
#ifdef TAHEAD1_SCMEDIUM
    for (int j = 0; j < ((1 << TAHEAD1_LOGBNB) - 1); j++) {
      TAHEAD1_BBIAS[j] = -(1 << (TAHEAD1_PERCWIDTH - 1)) + (random() & ((1 << TAHEAD1_PERCWIDTH) - 1));
    }
    for (int j = 0; j < ((1 << TAHEAD1_LOGFNB) - 1); j++) {
      TAHEAD1_FBIAS[j] = -(1 << (TAHEAD1_PERCWIDTH - 1)) + (random() & ((1 << TAHEAD1_PERCWIDTH) - 1));
    }
    for (int j = 0; j < ((1 << TAHEAD1_LOGINB) - 1); j++) {
      TAHEAD1_IBIAS[j] = -(1 << (TAHEAD1_PERCWIDTH - 1)) + (random() & ((1 << TAHEAD1_PERCWIDTH) - 1));
    }
    for (int j = 0; j < ((1 << TAHEAD1_LOGINB) - 1); j++) {
      TAHEAD1_IIBIAS[j] = -(1 << (TAHEAD1_PERCWIDTH - 1)) + (random() & ((1 << TAHEAD1_PERCWIDTH) - 1));
    }

#endif
    for (int j = 0; j < (1 << TAHEAD1_LOGBIAS); j++) {
      TAHEAD1_BiasPCLMAP[j] = -(1 << (TAHEAD1_PERCWIDTH - 1)) + (random() & ((1 << TAHEAD1_PERCWIDTH) - 1));
      TAHEAD1_BiasPC[j]     = -(1 << (TAHEAD1_PERCWIDTH - 1)) + (random() & ((1 << TAHEAD1_PERCWIDTH) - 1));
    }

#endif
  }

  // index function for the bimodal table

  int bindex(uint64_t PC) { return ((PC ^ (PC >> TAHEAD1_LOGB)) & ((1 << (TAHEAD1_LOGB)) - 1)); }
  // the index functions for the tagged tables uses path history as in the OBIAS predictor
  // F serves to mix path history: not very important impact

  int F(long long A, int size, int bank) {
    int A1, A2;
    A  = A & ((1 << size) - 1);
    A1 = (A & ((1 << TAHEAD1_LOGG) - 1));
    A2 = (A >> TAHEAD1_LOGG);
    if (bank < TAHEAD1_LOGG) {
      A2 = ((A2 << bank) & ((1 << TAHEAD1_LOGG) - 1)) ^ (A2 >> (TAHEAD1_LOGG - bank));
    }
    A = A1 ^ A2;
    if (bank < TAHEAD1_LOGG) {
      A = ((A << bank) & ((1 << TAHEAD1_LOGG) - 1)) ^ (A >> (TAHEAD1_LOGG - bank));
    }
    //  return(0);
    return (A);
  }

  // gindex computes a full hash of PC, TAHEAD1_ghist and TAHEAD1_phist
  uint gindex(unsigned int PC, int bank, long long hist, TAHEAD1_folded_history *ptahead1_ch_i) {
    uint index;
    int  logg  = TAHEAD1_LOGG + /* TAHEAD1_SHARED+*/ (TAHEAD1_SHARED & (bank <= 1));
    uint M     = (TAHEAD1_m[bank] > TAHEAD1_PHISTWIDTH) ? TAHEAD1_PHISTWIDTH : TAHEAD1_m[bank];
    index      = PC ^ (PC >> (abs(logg - bank) + 1)) ^ ptahead1_ch_i[bank].comp ^ F(hist, M, bank);
    uint32_t X = (index ^ (index >> logg) ^ (index >> 2 * logg)) & ((1 << logg) - 1);
#ifdef TAHEAD1_INTERLEAVED
    if (bank == 1) {
      BANK1 = index % TAHEAD1_NHIST;
    }
#endif

    return (X);
  }

  //  tag computation
  uint16_t gtag(unsigned int PC, int bank, TAHEAD1_folded_history *ch0, TAHEAD1_folded_history *ch1) {
    int tag = PC ^ (PC >> 2);
    int M   = (TAHEAD1_m[bank] > TAHEAD1_PHISTWIDTH) ? TAHEAD1_PHISTWIDTH : TAHEAD1_m[bank];
    tag     = (tag >> 1) ^ ((tag & 1) << 10) ^ F(TAHEAD1_phist, M, bank);
    tag ^= ch0[bank].comp ^ (ch1[bank].comp << 1);
    tag ^= tag >> TAHEAD1_TBITS;
    tag ^= (tag >> (TAHEAD1_TBITS - 2));

    return tag & ((1 << TAHEAD1_TBITS) - 1);
  }

  // up-down saturating counter
  void ctrupdate(int8_t &ctr, bool taken, int nbits) {
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

  bool getbim() {
    TAHEAD1_BIM      = (get_TAHEAD1_btable_entry(TAHEAD1_BI).pred)
                           ? (get_TAHEAD1_btable_entry(TAHEAD1_BI >> TAHEAD1_HYSTSHIFT).hyst)
                           : -1 - (get_TAHEAD1_btable_entry(TAHEAD1_BI >> TAHEAD1_HYSTSHIFT).hyst);
    TAHEAD1_TAGECONF = 3 * (get_TAHEAD1_btable_entry(TAHEAD1_BI >> TAHEAD1_HYSTSHIFT).hyst != 0);

    return (get_TAHEAD1_btable_entry(TAHEAD1_BI).pred != 0);
  }

  void baseupdate(bool Taken) {
    int8_t inter = TAHEAD1_BIM;
    ctrupdate(inter, Taken, TAHEAD1_BIMWIDTH);
    get_TAHEAD1_btable_entry(TAHEAD1_BI).pred                      = (inter >= 0);
    get_TAHEAD1_btable_entry(TAHEAD1_BI >> TAHEAD1_HYSTSHIFT).hyst = (inter >= 0) ? inter : -inter - 1;
  };
  uint32_t MYRANDOM() {
    // This pseudo-random function: just to be sure that the simulator is deterministic
    //  results are within +- 0.002 MPKI in average with some larger difference on individual benchmarks
    TAHEAD1_Seed++;
    TAHEAD1_Seed += TAHEAD1_phist;
    TAHEAD1_Seed = (TAHEAD1_Seed >> 21) + (TAHEAD1_Seed << 11);
    TAHEAD1_Seed += TAHEAD1_ptghist;
    TAHEAD1_Seed = (TAHEAD1_Seed >> 10) + (TAHEAD1_Seed << 22);
    TAHEAD1_Seed += TAHEAD1_GTAG[4];
    return (TAHEAD1_Seed);
  };

  //  TAGE PREDICTION: same code at fetch or retire time but the index and tags must recomputed
  void Tagepred(uint64_t PC) {
    TAHEAD1_HitBank    = 0;
    TAHEAD1_AltBank    = 0;
    TAHEAD1_HCpredBank = 0;
    if (TAHEAD1_Numero == 0) {
      for (int i = 1; i <= TAHEAD1_NHIST; i++) {
        TAHEAD1_AHGI[TAHEAD1_NPRED % 10][i]   = gindex(PC, i, TAHEAD1_phist, tahead1_ch_i);
        TAHEAD1_AHGTAG[TAHEAD1_NPRED % 10][i] = gtag(PC, i, TAHEAD1_ch_t[0], TAHEAD1_ch_t[1]);
      }
      if (TAHEAD1_SHARED) {
        int X = TAHEAD1_AHGI[TAHEAD1_NPRED % 10][1] & 1;
        for (int i = 2; i < TAHEAD1_NHIST/2; i++) {
          TAHEAD1_AHGI[TAHEAD1_NPRED % 10][i] <<= 1;
          TAHEAD1_AHGI[TAHEAD1_NPRED % 10][i] ^= X;
        }
        for (int i = TAHEAD1_NNHIST/2; i <= TAHEAD1_NHIST; i++) {
          TAHEAD1_AHGI[TAHEAD1_NPRED % 10][i] <<= 1;
          TAHEAD1_AHGI[TAHEAD1_NPRED % 10][i] ^= X ^ 1;
        }
      }
#ifdef TAHEAD1_INTERLEAVED
#ifndef TAHEAD1_ADJACENTTABLE
      for (int i = 1; i <= TAHEAD1_NHIST; i++) {
        TAHEAD1_AHGI[TAHEAD1_NPRED % 10][i] += ((BANK1 + i) % TAHEAD1_NHIST) * (1 << (TAHEAD1_LOGG));
      }
#else
      for (int i = 2; i <= TAHEAD1_NHIST; i += 2) {
        TAHEAD1_AHGI[TAHEAD1_NPRED % 10][i] = TAHEAD1_AHGI[TAHEAD1_NPRED % 10][i - 1];
      }
      for (int i = 1; i <= TAHEAD1_NHIST; i++) {
        TAHEAD1_AHGI[TAHEAD1_NPRED % 10][i]
            += ((BANK1 + ((i - 1) / 2)) % (TAHEAD1_NHIST / 2)) * (1 << (TAHEAD1_LOGG + 1)) + ((i & 1) << TAHEAD1_LOGG);
      }

#endif
#endif
    }
    int AHEADTAG = (TAHEAD1_AHEAD > 0) ? TAHEAD1_AHEAD - 1 : TAHEAD1_AHEAD;
    // assumes that the tag is used one cycle later than the index if TAHEAD1_AHEAD pipelining is used.

    TAHEAD1_BI
        = (TAHEAD1_PCBLOCK ^ ((TAHEAD1_Numero & (TAHEAD1_NBREADPERTABLE - 1)) << (TAHEAD1_LOGB - 2))) & ((1 << TAHEAD1_LOGB) - 1);

    // For TAHEAD1_AHEAD, one considers that the bimodal prediction is  obtained during the last cycle
    for (int i = 1; i <= TAHEAD1_NHIST; i++) {
#if (TAHEAD1_AHEAD != 0)
      {
        TAHEAD1_GI[i] = TAHEAD1_AHGI[(TAHEAD1_NPRED - TAHEAD1_AHEAD) % 10][i]
                        ^ (((TAHEAD1_GH ^ TAHEAD1_Numero ^ TAHEAD1_BI ^ (TAHEAD1_PCBLOCK >> 3)) & (TAHEAD1_READWIDTHAHEAD - 1))
                           << (TAHEAD1_LOGG - TAHEAD1_LOGASSOC - 4));
        // some bits are hashed on  values that are unknown at prediction read time: assumes READWITHTAHEAD1 reads at a time

        TAHEAD1_GI[i] *= TAHEAD1_ASSOC;
        TAHEAD1_GTAG[i] = TAHEAD1_AHGTAG[(TAHEAD1_NPRED - AHEADTAG) % 10][i]

                          ^ (((TAHEAD1_GH ^ (TAHEAD1_GH >> 1)) & (TAHEAD1_TAGCHECKAHEAD - 1)));

        ;

        // TAHEAD1_TAGCHECKAHEAD reads per read entry
      }

#else
      {
        TAHEAD1_GI[i] = TAHEAD1_AHGI[(TAHEAD1_NPRED - TAHEAD1_AHEAD) % 10][i]
                        ^ ((TAHEAD1_Numero & (TAHEAD1_NBREADPERTABLE - 1)) << (TAHEAD1_LOGG - TAHEAD1_LOGASSOC - 2));
        TAHEAD1_GI[i] *= TAHEAD1_ASSOC;
        TAHEAD1_GTAG[i] = TAHEAD1_AHGTAG[(TAHEAD1_NPRED - AHEADTAG) % 10][i] ^ (TAHEAD1_Numero);
      }
#endif
    }
#ifndef TAHEAD1_INTERLEAVED
#ifdef TAHEAD1_ADJACENTTABLE
    for (int i = 2; i <= TAHEAD1_NHIST; i += 2) {
      TAHEAD1_GI[i] = TAHEAD1_GI[i - 1];
    }

#endif
#endif
    for (int i = 1; i <= TAHEAD1_NHIST; i++) {
      for (int j = 0; j < TAHEAD1_ASSOC; j++) {
        TAHEAD1_GGI[j][i] = TAHEAD1_GI[i];
      }
      if (TAHEAD1_PSK == 1) {
        if (TAHEAD1_AHEAD == 0) {
          for (int j = 1; j < TAHEAD1_ASSOC; j++) {
            TAHEAD1_GGI[j][i] ^= ((TAHEAD1_GTAG[i] >> (3 + 2 * j)) & 0x3) << (TAHEAD1_LOGG - 3);
          }
        } else {
          for (int j = 1; j < TAHEAD1_ASSOC; j++) {
            TAHEAD1_GGI[j][i] ^= ((TAHEAD1_GTAG[i] >> (3 + 2 * j)) & (TAHEAD1_READWIDTHAHEAD - 1)) << (TAHEAD1_LOGG - 5);
          }
        }
      }

      // works for TAHEAD1_AHEAD also if TAHEAD1_READWIDTHAHEAD <= 16
    }

    TAHEAD1_alttaken         = getbim();
    TAHEAD1_HCpred           = TAHEAD1_alttaken;
    TAHEAD1_tage_pred        = TAHEAD1_alttaken;
    TAHEAD1_LongestMatchPred = TAHEAD1_alttaken;

    // Look for the bank with longest matching history
    for (int i = TAHEAD1_NHIST; i > 0; i--) {
      for (int j = 0; j < TAHEAD1_ASSOC; j++) {
        // int idx = (TAHEAD1_GGI[j][i] + j)%TAHEAD1_getTableSize(i);
        if (get_TAHEAD1_gtable_entry(i, TAHEAD1_GGI[j][i] + j).tag == TAHEAD1_GTAG[i]) {
          TAHEAD1_HitBank  = i;
          TAHEAD1_HitAssoc = j;

          TAHEAD1_LongestMatchPred
              = (get_TAHEAD1_gtable_entry(TAHEAD1_HitBank, TAHEAD1_GGI[TAHEAD1_HitAssoc][TAHEAD1_HitBank] + TAHEAD1_HitAssoc).ctr
                 >= 0);
          TAHEAD1_TAGECONF = (abs(2
                                      * get_TAHEAD1_gtable_entry(TAHEAD1_HitBank,
                                                                 TAHEAD1_GGI[TAHEAD1_HitAssoc][TAHEAD1_HitBank] + TAHEAD1_HitAssoc)
                                            .ctr
                                  + 1))
                             >> 1;

          break;
        }
      }
      if (TAHEAD1_HitBank > 0) {
        break;
      }
    }
    // should be noted that when TAHEAD1_LongestMatchPred is not low conf then TAHEAD1_alttaken is the 2nd not-low conf:  not a
    // critical path, needed only on update.
    for (int i = TAHEAD1_HitBank - 1; i > 0; i--) {
      for (int j = 0; j < TAHEAD1_ASSOC; j++) {
        if (get_TAHEAD1_gtable_entry(i, TAHEAD1_GGI[j][i] + j).tag == TAHEAD1_GTAG[i]) {
          // if (abs (2 * TAHEAD1_gtable[i][TAHEAD1_GGI[j][i] + j].ctr + 1) != 1)
          // slightly better to pick alternate prediction as not low confidence
          {
            TAHEAD1_AltAssoc = j;
            TAHEAD1_AltBank  = i;
            break;
          }
        }
      }
      if (TAHEAD1_AltBank > 0) {
        break;
      }
    }
    if (TAHEAD1_HitBank > 0) {
      if (abs(2 * get_TAHEAD1_gtable_entry(TAHEAD1_HitBank, TAHEAD1_GGI[TAHEAD1_HitAssoc][TAHEAD1_HitBank] + TAHEAD1_HitAssoc).ctr
              + 1)
          == 1) {
        for (int i = TAHEAD1_HitBank - 1; i > 0; i--) {
          for (int j = 0; j < TAHEAD1_ASSOC; j++) {
            if (get_TAHEAD1_gtable_entry(i, TAHEAD1_GGI[j][i] + j).tag == TAHEAD1_GTAG[i]) {
              if (abs(2 * get_TAHEAD1_gtable_entry(i, TAHEAD1_GGI[j][i] + j).ctr + 1) != 1)
              // slightly better to pick alternate prediction as not low confidence
              {
                TAHEAD1_HCpredBank = i;

                TAHEAD1_HCpredAssoc = j;
                TAHEAD1_HCpred      = (get_TAHEAD1_gtable_entry(i, TAHEAD1_GGI[j][i] + j).ctr >= 0);

                break;
              }
            }
          }
          if (TAHEAD1_HCpredBank > 0) {
            break;
          }
        }
      }

      else {
        TAHEAD1_HCpredBank  = TAHEAD1_HitBank;
        TAHEAD1_HCpredAssoc = TAHEAD1_HitAssoc;
        TAHEAD1_HCpred      = TAHEAD1_LongestMatchPred;
      }
    }

    // computes the prediction and the alternate prediction

    if (TAHEAD1_HitBank > 0) {
      if (TAHEAD1_AltBank > 0) {
        TAHEAD1_alttaken
            = (get_TAHEAD1_gtable_entry(TAHEAD1_AltBank, TAHEAD1_GGI[TAHEAD1_AltAssoc][TAHEAD1_AltBank] + TAHEAD1_AltAssoc).ctr
               >= 0);
      }

#ifndef TAHEAD1_SC
      // if the entry is recognized as a newly allocated entry and
      // USE_ALT_ON_NA is positive  use the alternate prediction
      bool Huse_alt_on_na = (TAHEAD1_use_alt_on_na >= 0);

      if ((!Huse_alt_on_na)
          || (abs(2
                      * get_TAHEAD1_gtable_entry(TAHEAD1_HitBank, TAHEAD1_GGI[TAHEAD1_HitAssoc][TAHEAD1_HitBank] + TAHEAD1_HitAssoc)
                            .ctr
                  + 1)
              > 1)) {
        TAHEAD1_tage_pred = TAHEAD1_LongestMatchPred;
      } else {
        TAHEAD1_tage_pred = TAHEAD1_HCpred;
      }

#else
      TAHEAD1_tage_pred = TAHEAD1_LongestMatchPred;
#endif
    }
  }

  // compute the prediction

  void fetchBoundaryEnd() {
#ifdef TAHEAD1_DELAY_UPDATE
    // TAHEAD1_Numero = 0;
#endif
  }

  bool getPrediction(uint64_t PCBRANCH, bool& bias, bool& lowconf) {
    (void)PCBRANCH;

    uint64_t PC = TAHEAD1_PCBLOCK ^ (TAHEAD1_Numero << 5);

    // computes the TAGE table addresses and the partial tags
    Tagepred(PC);
    TAHEAD1_pred_taken = TAHEAD1_tage_pred;
    TAHEAD1_predSC     = TAHEAD1_pred_taken;
    TAHEAD1_predTSC    = TAHEAD1_pred_taken;

    // printf("pc:%lx Num:%lx ptaken:%d\n", PC, TAHEAD1_Numero, TAHEAD1_pred_taken);
#ifdef TAHEAD1_DELAY_UPDATE
    TAHEAD1_Numero++;
#endif

#ifndef TAHEAD1_SC
#ifdef LMP
    return (TAHEAD1_LongestMatchPred);
#endif
    return TAHEAD1_pred_taken;
#endif
    if (TAHEAD1_AHEAD) {
      PC = TAHEAD1_PrevPCBLOCK ^ (TAHEAD1_Numero << 5) ^ (TAHEAD1_PrevNumero << 5) ^ ((TAHEAD1_BI & 3) << 5);
    }

    // Let us  compute the TAHEAD1_SC prediction
    TAHEAD1_SUMSC = 0;
////// These extra counters seem to bring a marginal  gain of 0.006 MPKI  when only pure TAHEAD1_SC, not useful when other info
#ifndef TAHEAD1_SCMEDIUM
    TAHEAD1_SUMSC += TAHEAD1_incval(TAHEAD1_BiasGEN);
    TAHEAD1_SUMSC += TAHEAD1_incval(TAHEAD1_BiasLM[TAHEAD1_LongestMatchPred]);
    TAHEAD1_SUMSC += TAHEAD1_incval(TAHEAD1_BiasAP[TAHEAD1_HCpred]);
#endif
    //////

    TAHEAD1_SUMSC += TAHEAD1_incval(TAHEAD1_BiasLMAP[TAHEAD1_INDBIASLMAP]);
    // x 2: a little bit better
    TAHEAD1_SUMSC += 2 * TAHEAD1_incval(TAHEAD1_BiasPC[TAHEAD1_INDBIASPC]);
    TAHEAD1_SUMSC += TAHEAD1_incval(TAHEAD1_BiasPCLMAP[TAHEAD1_INDBIASPCLMAP]);

    TAHEAD1_predTSC = (TAHEAD1_SUMSC >= 0);
    // when TAHEAD1_predTSC is correct we do not allocate any new entry
#ifdef TAHEAD1_SCMEDIUM
    TAHEAD1_SUMFULL = 0;
    TAHEAD1_SUMFULL += TAHEAD1_incval(TAHEAD1_IIBIAS[TAHEAD1_INDBIASIMLIBR]);
#ifdef TAHEAD1_SCFULL
    TAHEAD1_SUMFULL += TAHEAD1_incval(TAHEAD1_FBIAS[TAHEAD1_INDBIASFHIST]);
    TAHEAD1_SUMFULL += TAHEAD1_incval(TAHEAD1_BBIAS[TAHEAD1_INDBIASBHIST]);
    TAHEAD1_SUMFULL += TAHEAD1_incval(TAHEAD1_IBIAS[TAHEAD1_INDBIASIMLITA]);

#endif

    // x 2: a little bit better

    TAHEAD1_SUMSC += 2 * TAHEAD1_SUMFULL;
#endif
    bool SCPRED = (TAHEAD1_SUMSC >= 0);

#ifdef TAHEAD1_FORCEONHIGHCONF
    TAHEAD1_pred_taken
        = (TAHEAD1_TAGECONF != 3) || (abs(TAHEAD1_SUMSC) >= TAHEAD1_updatethreshold / 2) ? SCPRED : TAHEAD1_LongestMatchPred;
#else
    TAHEAD1_pred_taken = (TAHEAD1_SUMSC >= 0);
#endif

    TAHEAD1_predSC = (TAHEAD1_SUMSC >= 0);

    bias = (TAHEAD_TAGECONF >= 1);
    lowconf = (TAHEAD_TAGECONF == 1);
    return TAHEAD1_pred_taken;
  }

  void HistoryUpdate(uint64_t PCBRANCH, Opcode opType, bool taken, uint64_t branchTarget, int &Y, TAHEAD1_folded_history *H,
                     TAHEAD1_folded_history *G, TAHEAD1_folded_history *J) {
    int brtype;

    if ((TAHEAD1_Numero == TAHEAD1_MAXBR - 1) || (taken)) {
      TAHEAD1_GH = (TAHEAD1_GH << 2) ^ PCBRANCH;

      uint64_t PC = TAHEAD1_PCBLOCK ^ (TAHEAD1_Numero << 5);
      TAHEAD1_NPRED++;
      TAHEAD1_GH <<= 2;
      uint64_t Successor = (taken) ? branchTarget ^ (branchTarget >> 4) : (PCBRANCH + 1) ^ ((PCBRANCH + 1) >> 4);
      TAHEAD1_GH ^= ((TAHEAD1_Numero) ^ Successor);
      brtype = 0;

      switch (opType) {
        case Opcode::iBALU_RJUMP:    // OPTYPE_JMP_INDIRECT_UNCOND:
        case Opcode::iBALU_RCALL:    // OPTYPE_CALL_INDIRECT_UNCOND:
        case Opcode::iBALU_RBRANCH:  // OPTYPE_JMP_INDIRECT_COND:
        // case Opcode::iBALU_RCALL : 									//
        // OPTYPE_CALL_INDIRECT_COND:
        case Opcode::iBALU_RET:  // OPTYPE_RET_UNCOND:
                                 // case OPTYPE_RET_COND: Opcode::iBALU_RET
          brtype = 2;
          break;
        // case Opcode::iBALU_LCALL : 									// OPTYPE_CALL_DIRECT_COND:
        case Opcode::iBALU_LCALL:    // OPTYPE_CALL_DIRECT_UNCOND:
        case Opcode::iBALU_LBRANCH:  // OPTYPE_JMP_DIRECT_COND:
        case Opcode::iBALU_LJUMP:    // OPTYPE_JMP_DIRECT_UNCOND:
          brtype = 0;
          break;
        default: exit(1);
      }

      switch (opType) {
        case Opcode::iBALU_LBRANCH:  // OPTYPE_JMP_DIRECT_COND:
        // case Opcode::iBALU_LCALL : 									// OPTYPE_CALL_DIRECT_COND:
        case Opcode::iBALU_RBRANCH:  // OPTYPE_JMP_INDIRECT_COND:
          // case Opcode::iBALU_RCALL : 									//
          // OPTYPE_CALL_INDIRECT_COND: case Opcode::iBALU_RET :
          // // OPTYPE_RET_COND:
          brtype += 1;
          break;

        default:;
      }
#ifdef TAHEAD1_SCMEDIUM
      TAHEAD1_PrevF_TaIMLI = TAHEAD1_F_TaIMLI;
      TAHEAD1_PrevF_BrIMLI = TAHEAD1_F_BrIMLI;
      TAHEAD1_PrevBHIST    = TAHEAD1_BHIST;
      TAHEAD1_PrevFHIST    = TAHEAD1_FHIST;
      if (taken) {
        if (branchTarget > PCBRANCH) {
          TAHEAD1_FHIST = (TAHEAD1_FHIST << 3) ^ (branchTarget >> 2) ^ (PCBRANCH >> 1);
        }
      }

      // Caution: this  is quite  different from the Micro 2015 paper
      // rely on close target  and on close  branches : capture loop nests, see slide set
      if ((brtype & 2) == 0)
      // not indirect or return
      {
        if (taken) {
          if (branchTarget < PCBRANCH) {
            // allows to  finish an iteration at different points
            if (((branchTarget & 65535) >> 6) == TAHEAD1_LastBack) {
              if (TAHEAD1_TaIMLI < ((1 << TAHEAD1_LOGBIAS) - 1)) {
                TAHEAD1_TaIMLI++;
              }
            } else {
              TAHEAD1_BBHIST = (TAHEAD1_BBHIST << 1) ^ TAHEAD1_LastBack;
              TAHEAD1_TaIMLI = 0;
            }
            if (((PCBRANCH & 65535) >> 6) == TAHEAD1_LastBackPC)

            {
              if (TAHEAD1_BrIMLI < ((1 << TAHEAD1_LOGBIAS) - 1)) {
                TAHEAD1_BrIMLI++;
              }
            } else {
              TAHEAD1_BBHIST = (TAHEAD1_BBHIST << 1) ^ TAHEAD1_LastBackPC;
              TAHEAD1_BrIMLI = 0;
            }
            TAHEAD1_LastBack   = (branchTarget & 65535) >> 6;
            TAHEAD1_LastBackPC = (PCBRANCH & 65535) >> 6;
          }
        }
      }
#endif
      if (TAHEAD1_AHEAD) {
        // to hash with TAHEAD1_Numero
        TAHEAD1_PrevNumero = (TAHEAD1_Numero & 1) << 1;
        TAHEAD1_PrevNumero ^= (TAHEAD1_Numero >> 1);
      }

      TAHEAD1_Numero <<= 1;
      TAHEAD1_Numero += taken;

      int T = ((PC ^ (PC >> 2))) ^ TAHEAD1_Numero ^ (branchTarget >> 3);

      int PATH      = PC ^ (PC >> 2) ^ (PC >> 4) ^ (branchTarget) ^ (TAHEAD1_Numero << 3);
      TAHEAD1_phist = (TAHEAD1_phist << 4) ^ PATH;
      TAHEAD1_phist = (TAHEAD1_phist & ((1 << 27) - 1));

      for (int t = 0; t < 4; t++) {
        int DIR = (T & 1);
        T >>= 1;

        PATH >>= 1;
        Y--;
        TAHEAD1_ghist[Y & (TAHEAD1_HISTBUFFERLENGTH - 1)] = DIR;
        for (int i = 1; i <= TAHEAD1_NHIST; i++) {
          H[i].update(TAHEAD1_ghist, Y);
          G[i].update(TAHEAD1_ghist, Y);
          J[i].update(TAHEAD1_ghist, Y);
        }
      }

      TAHEAD1_Numero = 0;

      TAHEAD1_PrevPCBLOCK = TAHEAD1_PCBLOCK;
      TAHEAD1_PCBLOCK     = (taken) ? branchTarget : PCBRANCH + 1;
      TAHEAD1_PCBLOCK     = TAHEAD1_PCBLOCK ^ (TAHEAD1_PCBLOCK >> 4);

    } else {
      TAHEAD1_Numero++;
    }
    TAHEAD1_BHIST
        = (TAHEAD1_BrIMLI == 0) ? TAHEAD1_BBHIST : ((TAHEAD1_BBHIST & 15) + (TAHEAD1_BrIMLI << 6)) ^ (TAHEAD1_BrIMLI >> 4);
    TAHEAD1_F_TaIMLI = (TAHEAD1_TaIMLI == 0) || (TAHEAD1_BrIMLI == TAHEAD1_TaIMLI) ? (TAHEAD1_GH) : TAHEAD1_TaIMLI;
    TAHEAD1_F_BrIMLI = (TAHEAD1_BrIMLI == 0) ? (TAHEAD1_phist) : TAHEAD1_BrIMLI;

    if (TAHEAD1_AHEAD == 0) {
      TAHEAD1_PrevF_TaIMLI = TAHEAD1_F_TaIMLI;
      TAHEAD1_PrevF_BrIMLI = TAHEAD1_F_BrIMLI;
      TAHEAD1_PrevBHIST    = TAHEAD1_BHIST;
      TAHEAD1_PrevFHIST    = TAHEAD1_FHIST;
    }
  }

  // END UPDATE  HISTORIES

  // Tahead1 UPDATE

  void updatePredictor(uint64_t PCBRANCH, Opcode opType, bool resolveDir, bool predDir, uint64_t branchTarget) {
    // uint64_t PC = TAHEAD1_PCBLOCK ^ (TAHEAD1_Numero << 5);
    //
    // if (TAHEAD1_AHEAD) {
    //   PC = TAHEAD1_PrevPCBLOCK ^ (TAHEAD1_Numero << 5) ^ (TAHEAD1_PrevNumero << 5) ^ ((TAHEAD1_BI & 3) << 5);
    // }
    // bool DONE = false;
#ifndef TAHEAD1_DELAY_UPDATE
    (void)predDir;
#endif
#ifdef TAHEAD1_SC
    bool SCPRED = (TAHEAD1_SUMSC >= 0);
    if ((SCPRED != resolveDir) || ((abs(TAHEAD1_SUMSC) < TAHEAD1_updatethreshold)))

    {
      if (SCPRED != resolveDir) {
        if (TAHEAD1_updatethreshold < (1 << (TAHEAD1_WIDTHRES)) - 1) {
          TAHEAD1_updatethreshold += 1;
        }
      } else {
        if (TAHEAD1_updatethreshold > 0) {
          TAHEAD1_updatethreshold -= 1;
        }
      }

      ctrupdate(TAHEAD1_BiasGEN, resolveDir, TAHEAD1_PERCWIDTH);
      ctrupdate(TAHEAD1_BiasAP[TAHEAD1_HCpred], resolveDir, TAHEAD1_PERCWIDTH);
      ctrupdate(TAHEAD1_BiasLM[TAHEAD1_LongestMatchPred], resolveDir, TAHEAD1_PERCWIDTH);
      ctrupdate(TAHEAD1_BiasLMAP[TAHEAD1_INDBIASLMAP], resolveDir, TAHEAD1_PERCWIDTH);

      ctrupdate(TAHEAD1_BiasPC[TAHEAD1_INDBIASPC], resolveDir, TAHEAD1_PERCWIDTH);
      ctrupdate(TAHEAD1_BiasPCLMAP[TAHEAD1_INDBIASPCLMAP], resolveDir, TAHEAD1_PERCWIDTH);

#ifdef TAHEAD1_SCMEDIUM

      ctrupdate(TAHEAD1_IBIAS[TAHEAD1_INDBIASIMLITA], resolveDir, TAHEAD1_PERCWIDTH);
      ctrupdate(TAHEAD1_IIBIAS[TAHEAD1_INDBIASIMLIBR], resolveDir, TAHEAD1_PERCWIDTH);
      ctrupdate(TAHEAD1_BBIAS[TAHEAD1_INDBIASBHIST], resolveDir, TAHEAD1_PERCWIDTH);
      ctrupdate(TAHEAD1_FBIAS[TAHEAD1_INDBIASFHIST], resolveDir, TAHEAD1_PERCWIDTH);

#endif
    }

#endif

    // TAGE UPDATE
    bool ALLOC = (TAHEAD1_HitBank < TAHEAD1_NHIST);
    ALLOC &= (TAHEAD1_LongestMatchPred != resolveDir);
    ALLOC &= (TAHEAD1_predTSC != resolveDir);
    if (TAHEAD1_HitBank > 0) {
      if ((TAHEAD1_TAGECONF == 0) || ((MYRANDOM() & 3) == 0)) {
        ctrupdate(TAHEAD1_CountLowConf, (TAHEAD1_TAGECONF == 0), 7);
      }
    }

    //////////////////////////////////////////////////

    if (TAHEAD1_HitBank > 0) {
      bool PseudoNewAlloc
          = (abs(2
                     * get_TAHEAD1_gtable_entry(TAHEAD1_HitBank, TAHEAD1_GGI[TAHEAD1_HitAssoc][TAHEAD1_HitBank] + TAHEAD1_HitAssoc)
                           .ctr
                 + 1)
             <= 1);
      // an entry is considered as newly allocated if its prediction counter is weak

      if (PseudoNewAlloc) {
#ifndef TAHEAD1_SC
        if (TAHEAD1_LongestMatchPred == resolveDir) {
          ALLOC = false;
        }

        if (TAHEAD1_LongestMatchPred != TAHEAD1_HCpred) {
          ctrupdate(TAHEAD1_use_alt_on_na, (TAHEAD1_HCpred == resolveDir), TAHEAD1_ALTWIDTH);
          // pure TAGE only
        }
#endif
      }
    }

/////////////////////////
#ifdef TAHEAD1_FILTERALLOCATION
    // filter allocations: all of this is done at update, not on the critical path
    //  try to evaluate if the misprediction rate is above 1/9th

    if ((TAHEAD1_tage_pred != resolveDir) || ((MYRANDOM() & 31) < 4)) {
      ctrupdate(TAHEAD1_CountMiss11, (TAHEAD1_tage_pred != resolveDir), 8);
    }

    if (TAHEAD1_HitBank > 0) {
      bool PseudoNewAlloc = (TAHEAD1_TAGECONF == 0);

      if (PseudoNewAlloc) {
        // Here we count correct/wrong weak counters to guide allocation
        for (int i = TAHEAD1_HitBank / 4; i <= TAHEAD1_NHIST / 4; i++) {
          ctrupdate(TAHEAD1_COUNT50[i],
                    (resolveDir == TAHEAD1_LongestMatchPred),
                    7);  // more or less than 50 % good predictions on weak counters
          if ((TAHEAD1_LongestMatchPred != resolveDir) || ((MYRANDOM() & 31) > 1)) {
            ctrupdate(TAHEAD1_COUNT16_31[i],
                      (resolveDir == TAHEAD1_LongestMatchPred),
                      7);  // more or less than 16/31  good predictions on weak counters
          }
        }
      }
    }
    //  when allocating a new entry is unlikely to result in a good prediction, rarely allocate

    if ((TAHEAD1_COUNT50[(TAHEAD1_HitBank + 1) / 4] < 0)) {
      ALLOC &= ((MYRANDOM() & ((1 << (3)) - 1)) == 0);
    } else
      // the future allocated entry is not that likely to be correct
      if ((TAHEAD1_COUNT16_31[(TAHEAD1_HitBank + 1) / 4] < 0)) {
        ALLOC &= ((MYRANDOM() & ((1 << 1) - 1)) == 0);
      }
// The benefit is essentially to decrease the number of allocations
#endif

    if (ALLOC) {
      int MaxNALLOC = (TAHEAD1_CountMiss11 < 0) + 8 * (TAHEAD1_CountLowConf >= 0);
      // this TAHEAD1_CountLowConf is not very useful :-)

      int NA      = 0;
      int DEP     = TAHEAD1_HitBank + 1;
      int Penalty = 0;
      DEP += ((MYRANDOM() & 1) == 0);
      DEP += ((MYRANDOM() & 3) == 0);
      if (DEP == TAHEAD1_HitBank) {
        DEP = TAHEAD1_HitBank + 1;
      }

      bool First = true;
#ifdef TAHEAD1_FILTERALLOCATION
      bool Test  = false;
#endif

      for (int i = DEP; i <= TAHEAD1_NHIST; i++) {
#ifdef TAHEAD1_FILTERALLOCATION
        // works because the physical tables are shared
        if (TAHEAD1_SHARED) {
          if ((i > 8) & (!Test)) {
            Test = true;

            if ((TAHEAD1_CountMiss11 >= 0)) {
              if ((MYRANDOM() & 7) > 0) {
                break;
              }
            }
          }
        }
#endif
        bool done = false;
        uint j    = (MYRANDOM() % TAHEAD1_ASSOC);
        {
          bool REP[2]  = {false};
          int  IREP[2] = {0};
          bool MOVE[2] = {false};
          for (int J = 0; J < TAHEAD1_ASSOC; J++) {
            j++;
            j       = j % TAHEAD1_ASSOC;
            int idx = (TAHEAD1_GGI[j][i] + j) % TAHEAD1_getTableSize(i);

            if (get_TAHEAD1_gtable_entry(i, idx).u == 0) {
              REP[j]  = true;
              IREP[j] = idx;

            }

            else if (TAHEAD1_REPSK == 1) {
              if (TAHEAD1_AHEAD == 0) {
                IREP[j] = TAHEAD1_GGI[j][i] ^ ((((get_TAHEAD1_gtable_entry(i, idx).tag >> 5) & 3) << (TAHEAD1_LOGG - 3)) + (j ^ 1));
              } else {
                IREP[j] = TAHEAD1_GGI[j][i]
                          ^ ((((get_TAHEAD1_gtable_entry(i, idx).tag >> 5) & (TAHEAD1_READWIDTHAHEAD - 1)) << (TAHEAD1_LOGG - 5))
                             + (j ^ 1));
              }

              REP[j] = (get_TAHEAD1_gtable_entry(i, IREP[j]).u == 0);

              MOVE[j] = true;
            }

            if (REP[j]) {
              if ((((TAHEAD1_UWIDTH == 1)
                    && ((((MYRANDOM() & ((1 << (abs(2 * get_TAHEAD1_gtable_entry(i, idx).ctr + 1) >> 1)) - 1)) == 0))))
                   || (TAHEAD1_TICKH >= TAHEAD1_BORNTICK / 2))
                  || (TAHEAD1_UWIDTH == 2)) {
                done = true;
                if (MOVE[j]) {
                  get_TAHEAD1_gtable_entry(i, IREP[j]).u   = get_TAHEAD1_gtable_entry(i, idx).u;
                  get_TAHEAD1_gtable_entry(i, IREP[j]).tag = get_TAHEAD1_gtable_entry(i, idx).tag;
                  get_TAHEAD1_gtable_entry(i, IREP[j]).ctr = get_TAHEAD1_gtable_entry(i, idx).ctr;
                }

                get_TAHEAD1_gtable_entry(i, idx).tag = TAHEAD1_GTAG[i];
#ifndef TAHEAD1_FORCEU
                get_TAHEAD1_gtable_entry(i, idx).u = 0;
#else

                get_TAHEAD1_gtable_entry(i, idx).u
                    = ((TAHEAD1_UWIDTH == 2) || (TAHEAD1_TICKH >= TAHEAD1_BORNTICK / 2)) & (First ? 1 : 0);
#endif
                get_TAHEAD1_gtable_entry(i, idx).ctr = (resolveDir) ? 0 : -1;

                NA++;
                if ((i >= 3) || (!First)) {
                  MaxNALLOC--;
                }
                First = false;
                i += 2;
                i -= ((MYRANDOM() & 1) == 0);
                i += ((MYRANDOM() & 1) == 0);
                i += ((MYRANDOM() & 3) == 0);
                break;
              }
            }
          }
          if (MaxNALLOC < 0) {
            break;
          }
          if (!done) {
#ifdef TAHEAD1_FORCEU

            for (int jj = 0; jj < TAHEAD1_ASSOC; jj++) {
              {
                // some just allocated entries  have been set to useful
                int idxj = (TAHEAD1_GGI[jj][i] + jj) % TAHEAD1_getTableSize(i);
                if ((MYRANDOM() & ((1 << (1 + TAHEAD1_LOGASSOC + TAHEAD1_REPSK)) - 1)) == 0) {
                  if (abs(2 * get_TAHEAD1_gtable_entry(i, idxj).ctr + 1) == 1) {
                    if (get_TAHEAD1_gtable_entry(i, idxj).u == 1) {
                      get_TAHEAD1_gtable_entry(i, idxj).u--;
                    }
                  }
                }
                if (TAHEAD1_REPSK == 1) {
                  if ((MYRANDOM() & ((1 << (1 + TAHEAD1_LOGASSOC + TAHEAD1_REPSK)) - 1)) == 0) {
                    if (abs(2 * get_TAHEAD1_gtable_entry(i, IREP[jj]).ctr + 1) == 1) {
                      if (get_TAHEAD1_gtable_entry(i, IREP[jj]).u == 1) {
                        get_TAHEAD1_gtable_entry(i, IREP[jj]).u--;
                      }
                    }
                  }
                }
              }
            }
#endif
            Penalty++;
          }
        }

        //////////////////////////////////////////////
      }

// we set two counts to monitor: "time to reset u" and "almost time reset u": TAHEAD1_TICKH is useful only if TAHEAD1_UWIDTH =1
#ifndef TAHEAD1_PROTECTRECENTALLOCUSEFUL
      TAHEAD1_TICKH += Penalty - NA;
      TAHEAD1_TICK += Penalty - 2 * NA;
#else
      TAHEAD1_TICKH += 2 * Penalty - 3 * NA;
      TAHEAD1_TICK += Penalty - (2 + 2 * (TAHEAD1_CountMiss11 >= 0)) * NA;
#endif
      if (TAHEAD1_TICKH < 0) {
        TAHEAD1_TICKH = 0;
      }
      if (TAHEAD1_TICKH >= TAHEAD1_BORNTICK) {
        TAHEAD1_TICKH = TAHEAD1_BORNTICK;
      }

      if (TAHEAD1_TICK < 0) {
        TAHEAD1_TICK = 0;
      }
      if (TAHEAD1_TICK >= TAHEAD1_BORNTICK) {
#ifndef TAHEAD1_INTERLEAVED
        // the simulator was designed for TAHEAD1_NHIST= 14
        if (TAHEAD1_NHIST == 14) {
          for (int i = 1; i <= ((TAHEAD1_SHARED) ? 8 : 14); i++) {
            for (int j = 0; j < TAHEAD1_ASSOC * (1 << (TAHEAD1_LOGG + (TAHEAD1_SHARED ? (i <= 6) : 0))); j++) {
              int idxx = j % TAHEAD1_getTableSize(i);
              // this is not realistic: in a real processor:    TAHEAD1_gtable[1][idxx].u >>= 1;
              if (get_TAHEAD1_gtable_entry(i, idxx).u > 0) {
                get_TAHEAD1_gtable_entry(i, idxx).u--;
              }
            }
          }
        }

        else {
          for (int i = 1; i <= TAHEAD1_NHIST; i++) {
            for (int j = 0; j < TAHEAD1_ASSOC * (1 << TAHEAD1_LOGG); j++) {
              int idxx = j % TAHEAD1_getTableSize(i);
              // this is not realistic: in a real processor:    TAHEAD1_gtable[1][idxx].u >>= 1;
              if (get_TAHEAD1_gtable_entry(i, idxx).u > 0) {
                get_TAHEAD1_gtable_entry(i, idxx).u--;
              }
            }
          }
        }
#else

        for (int j = 0; j < TAHEAD1_ASSOC * (1 << TAHEAD1_LOGG) * TAHEAD1_NHIST; j++) {
          int idxx = j % TAHEAD1_getTableSize(1);
          // this is not realistic: in a real processor:    TAHEAD1_gtable[1][idxx].u >>= 1;
          if (get_TAHEAD1_gtable_entry(1, idxx).u > 0) {
            get_TAHEAD1_gtable_entry(1, idxx).u--;
          }
        }
#endif
        TAHEAD1_TICK  = 0;
        TAHEAD1_TICKH = 0;
      }
    }

    // update TAGE predictions

    if (TAHEAD1_HitBank > 0) {
#ifdef TAHEAD1_UPDATEALTONWEAKMISP
      // This protection, when prediction is low confidence
      if (TAHEAD1_TAGECONF == 0) {
        if (TAHEAD1_LongestMatchPred != resolveDir) {
          if (TAHEAD1_AltBank != TAHEAD1_HCpredBank) {
            ctrupdate(
                get_TAHEAD1_gtable_entry(TAHEAD1_AltBank, TAHEAD1_GGI[TAHEAD1_AltAssoc][TAHEAD1_AltBank] + TAHEAD1_AltAssoc).ctr,
                resolveDir,
                TAHEAD1_CWIDTH);
          }
          if (TAHEAD1_HCpredBank > 0) {
            ctrupdate(get_TAHEAD1_gtable_entry(TAHEAD1_HCpredBank,
                                               TAHEAD1_GGI[TAHEAD1_HCpredAssoc][TAHEAD1_HCpredBank] + TAHEAD1_HCpredAssoc)
                          .ctr,
                      resolveDir,
                      TAHEAD1_CWIDTH);
          }

          else {
            baseupdate(resolveDir);
          }
        }
      }

#endif
      ctrupdate(get_TAHEAD1_gtable_entry(TAHEAD1_HitBank, TAHEAD1_GGI[TAHEAD1_HitAssoc][TAHEAD1_HitBank] + TAHEAD1_HitAssoc).ctr,
                resolveDir,
                TAHEAD1_CWIDTH);

    } else {
      baseupdate(resolveDir);
    }
    ////////: note that here it is TAHEAD1_alttaken that is used: the second hitting entry

    if (TAHEAD1_LongestMatchPred != TAHEAD1_alttaken) {
      if (TAHEAD1_LongestMatchPred == resolveDir) {
#ifdef TAHEAD1_PROTECTRECENTALLOCUSEFUL

        if (get_TAHEAD1_gtable_entry(TAHEAD1_HitBank, TAHEAD1_GGI[TAHEAD1_HitAssoc][TAHEAD1_HitBank] + TAHEAD1_HitAssoc).u == 0) {
          get_TAHEAD1_gtable_entry(TAHEAD1_HitBank, TAHEAD1_GGI[TAHEAD1_HitAssoc][TAHEAD1_HitBank] + TAHEAD1_HitAssoc).u++;
        }
        // Recent useful will survive a smart reset
#endif
        if (get_TAHEAD1_gtable_entry(TAHEAD1_HitBank, TAHEAD1_GGI[TAHEAD1_HitAssoc][TAHEAD1_HitBank] + TAHEAD1_HitAssoc).u
            < (1 << TAHEAD1_UWIDTH) - 1) {
          get_TAHEAD1_gtable_entry(TAHEAD1_HitBank, TAHEAD1_GGI[TAHEAD1_HitAssoc][TAHEAD1_HitBank] + TAHEAD1_HitAssoc).u++;
        }

      } else {
        if (get_TAHEAD1_gtable_entry(TAHEAD1_HitBank, TAHEAD1_GGI[TAHEAD1_HitAssoc][TAHEAD1_HitBank] + TAHEAD1_HitAssoc).u > 0) {
          if (TAHEAD1_predSC == resolveDir) {
            get_TAHEAD1_gtable_entry(TAHEAD1_HitBank, TAHEAD1_GGI[TAHEAD1_HitAssoc][TAHEAD1_HitBank] + TAHEAD1_HitAssoc).u--;
          }
        }
      }
    }

#ifdef TAHEAD1_DELAY_UPDATE
    (void)PCBRANCH;
    (void)predDir;
    (void)opType;
    (void)branchTarget;
    TAHEAD1_Numero++;
#else
    HistoryUpdate(PCBRANCH, opType, resolveDir, branchTarget, TAHEAD1_ptghist, tahead1_ch_i, TAHEAD1_ch_t[0], TAHEAD1_ch_t[1]);
#endif
  }

  void TrackOtherInst(uint64_t PCBRANCH, Opcode opType, bool taken, uint64_t branchTarget) {
#ifdef TAHEAD1_DELAY_UPDATE
    (void)PCBRANCH;
    (void)opType;
    (void)taken;
    (void)branchTarget;
    TAHEAD1_Numero++;
#else
    HistoryUpdate(PCBRANCH, opType, taken, branchTarget, TAHEAD1_ptghist, tahead1_ch_i, TAHEAD1_ch_t[0], TAHEAD1_ch_t[1]);
#endif
  }
#ifdef TAHEAD1_DELAY_UPDATE
  void delayed_history(uint64_t PCBRANCH, Opcode opType, bool taken, uint64_t branchTarget) {
    HistoryUpdate(PCBRANCH, opType, taken, branchTarget, TAHEAD1_ptghist, tahead1_ch_i, TAHEAD1_ch_t[0], TAHEAD1_ch_t[1]);
  }
#endif
};

#endif
