// Developped by A. Seznec
// This simulator corresponds to the slide set "TAGE: an engineering cookbook by Andre Seznec, November 2024"

// it was developped usiung the framework used for CBP 2016
// file main.cc was  modified to stop the simulation after 10000000 branches for all benchmarks and to potentially accomodate the
// option WARM
#ifndef _TAHEAD_H
#define _TAHEAD_H

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
// #define TAHEAD_RANDINIT  // TAHEAD_RANDINIT provide random values in all counters, might be slightly more realistic than
// initialization with weak counters

// Possible conf option if updates are delayed to end of fetch_boundary (BPred.cpp:pending)
// #define TAHEAD_DELAY_UPDATE 1

#define TAHEAD_LOGSCALE 2
#define TAHEAD_LOGT     (8 + TAHEAD_LOGSCALE)   /* logsize of a logical  TAGE tables */
#define TAHEAD_LOGB     (11 + TAHEAD_LOGSCALE)  // log of number of entries in bimodal predictor
#define TAHEAD_LOGBIAS  (7 + TAHEAD_LOGSCALE)   // logsize of tables in TAHEAD_SC

#if (TAHEAD_LOGSCALE == 4)
#define TAHEAD_MINHIST 2
#define TAHEAD_MAXHIST 350
#endif
#if (TAHEAD_LOGSCALE == 3)
#define TAHEAD_MINHIST 2
#define TAHEAD_MAXHIST 250
#endif
#if (TAHEAD_LOGSCALE == 2)
#define TAHEAD_MINHIST 2
#define TAHEAD_MAXHIST 250
#endif
#if (TAHEAD_LOGSCALE == 1)
#define TAHEAD_MINHIST 2
#define TAHEAD_MAXHIST 250
#endif

#if (TAHEAD_LOGSCALE == 0)
#define TAHEAD_MINHIST 1
#define TAHEAD_MAXHIST 250
#endif

#define TAHEAD_MAXBR          16  // Maximum TAHEAD_MAXBR  branches in  the block; the code assumes TAHEAD_MAXBR is a power of 2
#define TAHEAD_NBREADPERTABLE 4   // predictions read per table for a block

#define TAHEAD_AHEAD 2
// in the curent version:  only 0 or 2 are valid (0 corresponds to the conventional 1-block ahead, 2 coresponds to the 3-block
// ahead)

// FIXME: DISSABLING code
// general prameters
// Only useful when TAHEAD_AHEAD==2
#define TAHEAD_READWIDTHAHEAD 16  // the number of entries read in each tagged table   (per way if associative),
#define TAHEAD_TAGCHECKAHEAD  4   // the number of tag checks per entries,
//  (16,4) and (8,8) seems good design points

#define TAHEAD_NHIST 14  // 14  different history lengths, but 7 physical tables

#define TAHEAD_UWIDTH 2
#define TAHEAD_LOGASSOC \
  1  // associative tagged tables are probably  not worth the effort at TAHEAD_TBITS=12 : about 0.02 MPKI gain for associativity 2;
     // an extra tag bit would be  needed to get some gain with associativity 4 // but partial skewed associativity (option
     // TAHEAD_PSK) might be interesting
#define TAHEAD_TBITS 12  // if 11 bits: benefit from associativity vanishes

#define TAHEAD_LOGG  (TAHEAD_LOGT - TAHEAD_LOGASSOC)  // size of way in a logical TAGE table
#define TAHEAD_ASSOC (1 << TAHEAD_LOGASSOC)

#define TAHEAD_HYSTSHIFT 1  // bimodal hysteresis shared among (1<< TAHEAD_HYSTSHIFT) entries
#define TAHEAD_BIMWIDTH  3  //  with of the counter in the bimodal predictor
// A. Seznec: I just played using 3-bit counters in the simulator, using 2-bit counters but TAHEAD_HYSTSHIFT=0 brings similar
// accuracy

/////////////////////////////////////////////
// Options  for optimizations of TAGE
// #define TAHEAD_INTERLEAVED // just to show that it  is not  fully interleaving the banks is not that great and probably not worth
// the extra 14x14 shuffling/reshuffling
#ifdef TAHEAD_INTERLEAVED
#define TAHEAD_SHARED        0
#define TAHEAD_ADJACENTTABLE 1
int BANK1;
#endif

/////////////////////////////////////////////////
// the replacement/allocation policies described in the slide set
//#define TAHEAD_OPTTAGE
#ifdef TAHEAD_OPTTAGE
#ifndef TAHEAD_INTERLEAVED
#define TAHEAD_ADJACENTTABLE \
  1  // ~+0.076,  if 14 tables :7 physical tables: Logical table T(2i-1) and T(2i) are mapped on the the same physical P(i), but the
     // two predictions are adjacent and  are read with index computed with H(2i-1), the tags are respectively computed with  for
     // H(2i-1) and H(2i).
#define TAHEAD_SHARED 1  // (T1/T9) (T2/T10)   shared the same bank T9 and T10 do not share with anybody: ~ -0.076 MPKI
#endif
#define TAHEAD_OPTGEOHIST  // we can do better than geometric series
// Optimizations  allocation/replacement: globally; ~0.09
#define TAHEAD_FILTERALLOCATION 1  // ~ -0.04 MPKI
#define TAHEAD_FORCEU           1  // don't work if only one U  bit	// from times selective allocation with u = 1: ~0.015 MPKI

#if (TAHEAD_LOGASSOC == 1)
// A. Seznec: partial skewed associativity, remmeber that I invented it in 1993 :-)
#define TAHEAD_PSK 1
#define TAHEAD_REPSK \
  1  // this optimization is funny, if no "useless" entry, move the entry on the other way to make room, brings a little bit of
     // accuracy
#else
#define TAHEAD_PSK   1
#define TAHEAD_REPSK 0
#endif

#define TAHEAD_PROTECTRECENTALLOCUSEFUL 1  // Recently allocated entries  are protected against the smart u reset: ~ 0.007 MPKI
#define TAHEAD_UPDATEALTONWEAKMISP \
  1  // When the Longest match is weak and wrong, one updates also the alternate prediction and HCPred : ~0.018 MPKI

#else
#define TAHEAD_SHARED 0
#define TAHEAD_PSK    0
#define TAHEAD_REPSK  0
#endif
//////////////////////////////////////////////

/////////////////////////////////////////////
/// For the TAHEAD_SC component
#define TAHEAD_SC  // Enables the statistical corrector
#ifndef TAHEAD_SC
#define LMP  // systematically use TAHEAD_LongestMatchPred, but with an optimized allocation policy.
// In practice the optimizations on TAGE brings significant gains
#endif

#define TAHEAD_FORCEONHIGHCONF  //   if TAGE is high conf and TAHEAD_SC very low conf then use TAGE, if TAHEAD_SC: brings 0.008 -
                                //   0.016 MPKI, but a
                                //   5-to-1 mux instead a 4-to-1
// #define TAHEAD_MORESCLOGICAHEAD // if TAHEAD_AHEAD and if TAHEAD_SC uses four times  the number of adder trees (compute 16 SCsum
// per prediction !), ~ 1 % gain in accuracy

// Add the extra TAHEAD_SC tables
#define TAHEAD_SCMEDIUM
#ifdef TAHEAD_SCMEDIUM
#define TAHEAD_SCFULL
// 4 tables for IMLI and global history variation: see slide set
#endif

#define TAHEAD_PERCWIDTH 6  // Statistical corrector counter width: if FULL  6 bits brings 0.007
/////////////////////////////////////////////////

int TAHEAD_NPRED = 20;  // this variable needs to be larger than TAHEAD_AHEAD to avoid core dump when TAHEAD_AHEAD prediction
// I was wanting to test large TAHEAD_AHEAD distances up to 9
uint     TAHEAD_AHGI[10][TAHEAD_NHIST + 1];    // indexes to the different tables are computed only once
uint     TAHEAD_AHGTAG[10][TAHEAD_NHIST + 1];  // tags for the different tables are computed only once
uint64_t TAHEAD_Numero;                        // Number of the branch in the basic block
uint64_t TAHEAD_PCBLOCK;
uint64_t TAHEAD_PrevPCBLOCK;
uint64_t TAHEAD_PrevNumero;

// To get the predictor storage budget on stderr  uncomment the next line
#define TAHEAD_PRINTSIZE
#include <vector>

//////////////////////////////////
////////The statistical corrector components

// The base table  in the TAHEAD_SC component indexed with only PC + information flowing out from  TAGE
//  In order to  allow computing SCSUM in parallel with TAGE check, only TAHEAD_LongestMatchPred and TAHEAD_HCpred are used. 4 SCSUM
//  are computed, and a final 4-to-1 selects the correct prediction:   each extra bit of information (confidence, etc) would
//  necessitate  doubling the number of computed SCSUMs and double the width of the final MUX

// if only PC-based TAHEAD_SC these ones are useful
int8_t TAHEAD_BiasGEN;
int8_t TAHEAD_BiasAP[2];
int8_t TAHEAD_BiasLM[2];
//////

int8_t TAHEAD_BiasLMAP[4];
int8_t TAHEAD_BiasPC[1 << TAHEAD_LOGBIAS];
int8_t TAHEAD_BiasPCLMAP[(1 << TAHEAD_LOGBIAS)];

#define TAHEAD_LOGINB TAHEAD_LOGBIAS
int    TAHEAD_Im = TAHEAD_LOGBIAS;
int8_t TAHEAD_IBIAS[(1 << TAHEAD_LOGINB)];
int8_t TAHEAD_IIBIAS[(1 << TAHEAD_LOGINB)];

// Back path history; (in practice  when a  new backward branch is  reached; 2 bits are pushed in the history
#define TAHEAD_LOGBNB TAHEAD_LOGBIAS
int    TAHEAD_Bm = TAHEAD_LOGBIAS;
int8_t TAHEAD_BBIAS[(1 << TAHEAD_LOGBNB)];
//////////////// Forward path history (taken)
#define TAHEAD_LOGFNB TAHEAD_LOGBIAS
int    TAHEAD_Fm = TAHEAD_LOGBIAS;
int8_t TAHEAD_FBIAS[(1 << TAHEAD_LOGFNB)];

// indices for the  TAHEAD_SC tables
#define TAHEAD_INDBIASLMAP (TAHEAD_LongestMatchPred + (TAHEAD_HCpred << 1))
#define TAHEAD_PSNUM \
  ((((TAHEAD_AHEAD) ? ((TAHEAD_Numero ^ TAHEAD_PCBLOCK) & (TAHEAD_MAXBR - 1)) : (TAHEAD_Numero & (TAHEAD_MAXBR - 1)))) << 2)

#ifdef TAHEAD_MORESCLOGICAHEAD
#define TAHEAD_PCBL ((TAHEAD_AHEAD) ? (TAHEAD_PrevPCBLOCK ^ ((TAHEAD_GH)&3)) : (TAHEAD_PCBLOCK))
#else
#define TAHEAD_PCBL ((TAHEAD_AHEAD) ? (TAHEAD_PrevPCBLOCK) : (TAHEAD_PCBLOCK))
#endif

#define TAHEAD_INDBIASPC     (((((TAHEAD_PCBL ^ (TAHEAD_PCBL >> (TAHEAD_LOGBIAS - 5))))) & ((1 << TAHEAD_LOGBIAS) - 1)) ^ TAHEAD_PSNUM)
#define TAHEAD_INDBIASPCLMAP (TAHEAD_INDBIASPC) ^ ((TAHEAD_LongestMatchPred ^ (TAHEAD_HCpred << 1)) << (TAHEAD_LOGBIAS - 2))
// a single  physical table but  two logic tables: indices agree on all the bits except 2

#define TAHEAD_INDBIASBHIST \
  (((((TAHEAD_PCBL ^ TAHEAD_PrevBHIST ^ (TAHEAD_PCBL >> (TAHEAD_LOGBIAS - 4))))) & ((1 << TAHEAD_LOGBNB) - 1)) ^ TAHEAD_PSNUM)
#define TAHEAD_INDBIASFHIST \
  (((((TAHEAD_PCBL ^ TAHEAD_PrevFHIST ^ (TAHEAD_PCBL >> (TAHEAD_LOGBIAS - 3))))) & ((1 << TAHEAD_LOGFNB) - 1)) ^ TAHEAD_PSNUM)
#define TAHEAD_INDBIASIMLIBR \
  (((((TAHEAD_PCBL ^ TAHEAD_PrevF_BrIMLI ^ (TAHEAD_PCBL >> (TAHEAD_LOGBIAS - 6))))) & ((1 << TAHEAD_LOGINB) - 1)) ^ TAHEAD_PSNUM)
#define TAHEAD_INDBIASIMLITA                                                                                             \
  ((((((TAHEAD_PCBL >> 4) ^ TAHEAD_PrevF_TaIMLI ^ (TAHEAD_PCBL << (TAHEAD_LOGBIAS - 4))))) & ((1 << TAHEAD_LOGINB) - 1)) \
   ^ TAHEAD_PSNUM)

//////////////////////IMLI RELATED and backward/Forward history////////////////////////////////////
long long TAHEAD_TaIMLI;    // use to monitor the iteration number (based on target locality for backward branches)
long long TAHEAD_BrIMLI;    // use to monitor the iteration number (a second version based on backward branch locality))
long long TAHEAD_F_TaIMLI;  // use to monitor the iteration number,TAHEAD_BHIST if TAHEAD_TaIMLI = 0
long long TAHEAD_F_BrIMLI;  // use to monitor the iteration number (a second version), TAHEAD_FHIST if TAHEAD_BrIMLI = 0
long long TAHEAD_BHIST;
long long TAHEAD_FHIST;

// Same thing but a cycle TAHEAD_AHEAD
long long TAHEAD_PrevF_TaIMLI;  // use to monitor the iteration number, TAHEAD_BHIST if TAHEAD_TaIMLI = 0
long long TAHEAD_PrevF_BrIMLI;  // use to monitor the iteration number (a second version), TAHEAD_FHIST if TAHEAD_BrIMLI = 0
long long TAHEAD_PrevBHIST;
long long TAHEAD_PrevFHIST;

// Needs for computing the "histories" for IMLI and backward/forward histories
uint64_t TAHEAD_LastBack;
uint64_t TAHEAD_LastBackPC;
uint64_t TAHEAD_BBHIST;

// update threshold for the statistical corrector
#define TAHEAD_WIDTHRES 8
int TAHEAD_updatethreshold;

int TAHEAD_SUMSC;
int TAHEAD_SUMFULL;

bool TAHEAD_predTSC;
bool TAHEAD_predSC;
bool TAHEAD_pred_inter;

////  FOR TAGE //////

#define TAHEAD_HISTBUFFERLENGTH 4096  // we use a 4K entries history buffer to store the branch history

#define TAHEAD_BORNTICK 4096
// for the allocation policy

// utility class for index computation
// this is the cyclic shift register for folding
// a long global history into a smaller number of bits; see P. Michaud's PPM-like predictor at CBP-1

class TAHEAD_folded_history {
public:
  unsigned comp;
  int      CLENGTH;
  int      OLENGTH;
  int      OUTPOINT;
  int      INTEROUT;

  TAHEAD_folded_history() {}

  void init(int original_length, int compressed_length, int N) {
    (void)N;
    comp     = 0;
    OLENGTH  = original_length;
    CLENGTH  = compressed_length;
    OUTPOINT = OLENGTH % CLENGTH;
  }

  void update(uint8_t *h, int PT) {
    comp = (comp << 1) ^ h[PT & (TAHEAD_HISTBUFFERLENGTH - 1)];

    comp ^= h[(PT + OLENGTH) & (TAHEAD_HISTBUFFERLENGTH - 1)] << OUTPOINT;
    comp ^= (comp >> CLENGTH);
    comp = (comp) & ((1 << CLENGTH) - 1);
  }
};

class TAHEAD_bentry  // TAGE bimodal table entry
{
public:
  int8_t hyst;
  int8_t pred;
  TAHEAD_bentry() {
    pred = 0;
    hyst = 1;
  }
};

class TAHEAD_gentry  // TAGE global table entry
{
public:
  int8_t ctr;
  uint   tag;
  int8_t u;

  TAHEAD_gentry() {
    ctr = 0;
    u   = 0;
    tag = 0;
  }
};

bool TAHEAD_alttaken;  // alternate   TAGE prediction if the longest match was not hitting: needed for updating the u bit
bool TAHEAD_HCpred;    // longest not low confident match or base prediction if no confident match

bool   TAHEAD_tage_pred;  // TAGE prediction
bool   TAHEAD_LongestMatchPred;
int    TAHEAD_HitBank;     // longest matching bank
int    TAHEAD_AltBank;     // alternate matching bank
int    TAHEAD_HCpredBank;  // longest non weak  matching bank
int    TAHEAD_HitAssoc;
int    TAHEAD_AltAssoc;
int    TAHEAD_HCpredAssoc;
int    TAHEAD_Seed;  // for the pseudo-random number generator
int8_t TAHEAD_BIM;   // the bimodal prediction

int8_t TAHEAD_CountMiss11  = -64;  // more or less than 11% of misspredictions
int8_t TAHEAD_CountLowConf = 0;

int8_t TAHEAD_COUNT50[TAHEAD_NHIST + 1];     // more or less than 50%  misprediction on weak TAHEAD_LongestMatchPred
int8_t TAHEAD_COUNT16_31[TAHEAD_NHIST + 1];  // more or less than 16/31th  misprediction on weak TAHEAD_LongestMatchPred
int    TAHEAD_TAGECONF;                      // TAGE confidence  from 0 (weak counter) to 3 (saturated)

#define TAHEAD_PHISTWIDTH 27  // width of the path history used in TAGE
#define TAHEAD_CWIDTH     3   // predictor counter width on the TAGE tagged tables

// the counter(s) to chose between longest match and alternate prediction on TAGE when weak counters: only plain TAGE
#define TAHEAD_ALTWIDTH 5
int8_t TAHEAD_use_alt_on_na;
int    TAHEAD_TICK, TAHEAD_TICKH;  // for the reset of the u counter

uint8_t TAHEAD_ghist[TAHEAD_HISTBUFFERLENGTH];
int     TAHEAD_ptghist;
// for managing global path history

long long             TAHEAD_phist;                      // path history
int                   TAHEAD_GH;                         //  another form of path history
TAHEAD_folded_history tahead_ch_i[TAHEAD_NHIST + 1];     // utility for computing TAGE indices
TAHEAD_folded_history TAHEAD_ch_t[2][TAHEAD_NHIST + 1];  // utility for computing TAGE tags

// For the TAGE predictor
TAHEAD_bentry *TAHEAD_btable;                    // bimodal TAGE table
TAHEAD_gentry *TAHEAD_gtable[TAHEAD_NHIST + 1];  // tagged TAGE tables
int            TAHEAD_m[TAHEAD_NHIST + 1];
uint           TAHEAD_GI[TAHEAD_NHIST + 1];                 // indexes to the different tables are computed only once
uint           TAHEAD_GGI[TAHEAD_ASSOC][TAHEAD_NHIST + 1];  // indexes to the different tables are computed only once
uint           TAHEAD_GTAG[TAHEAD_NHIST + 1];               // tags for the different tables are computed only once
int            TAHEAD_BI;                                   // index of the bimodal table
bool           TAHEAD_pred_taken;                           // prediction

int TAHEAD_incval(int8_t ctr) {
  return (2 * ctr + 1);
  // to center the sum
  //  probably not worth, but don't understand why
}

int TAHEAD_predictorsize() {
  int STORAGESIZE = 0;
  int inter       = 0;

  STORAGESIZE += TAHEAD_NHIST * (1 << TAHEAD_LOGG) * (TAHEAD_CWIDTH + TAHEAD_UWIDTH + TAHEAD_TBITS) * TAHEAD_ASSOC;
#ifndef TAHEAD_SC
  STORAGESIZE += TAHEAD_ALTWIDTH;
  // the use_alt counter
#endif
  STORAGESIZE += (1 << TAHEAD_LOGB) + (TAHEAD_BIMWIDTH - 1) * (1 << (TAHEAD_LOGB - TAHEAD_HYSTSHIFT));
  STORAGESIZE += TAHEAD_m[TAHEAD_NHIST];      // the history bits
  STORAGESIZE += TAHEAD_PHISTWIDTH;           // TAHEAD_phist
  STORAGESIZE += 12;                          // the TAHEAD_TICK counter
  STORAGESIZE += 12;                          // the TAHEAD_TICKH counter
  STORAGESIZE += 2 * 7 * (TAHEAD_NHIST / 4);  // counters TAHEAD_COUNT50 TAHEAD_COUNT16_31
  STORAGESIZE += 8;                           // TAHEAD_CountMiss11
  STORAGESIZE += 36;                          // for the random number generator
  fprintf(stderr, " (TAGE %d) ", STORAGESIZE);
#ifdef TAHEAD_SC

  inter += TAHEAD_WIDTHRES;
  inter += (TAHEAD_PERCWIDTH)*2 * (1 << TAHEAD_LOGBIAS);  // TAHEAD_BiasPC and TAHEAD_BiasPCLMAP,
  inter += (TAHEAD_PERCWIDTH)*2;                          // TAHEAD_BiasLMAP

#ifdef TAHEAD_SCMEDIUM
#ifdef TAHEAD_SCFULL

  inter += (1 << TAHEAD_LOGFNB) * TAHEAD_PERCWIDTH;
  inter += TAHEAD_LOGFNB;
  inter += (1 << TAHEAD_LOGBNB) * TAHEAD_PERCWIDTH;
  inter += TAHEAD_LOGBNB;
  inter += (1 << TAHEAD_LOGINB) * TAHEAD_PERCWIDTH;  // two forms
  inter += TAHEAD_LOGBIAS;
  inter += 10;  // TAHEAD_LastBackPC
#endif
  inter += (1 << TAHEAD_LOGINB) * TAHEAD_PERCWIDTH;  // two forms
  inter += TAHEAD_LOGBIAS;
  inter += 10;  // TAHEAD_LastBack
#endif

  STORAGESIZE += inter;

  fprintf(stderr, " (TAHEAD_SC %d) ", inter);
#endif
#ifdef TAHEAD_PRINTSIZE

  fprintf(stderr, " (TOTAL %d, %d Kbits)\n  ", STORAGESIZE, STORAGESIZE / 1024);
  fprintf(stdout, " (TOTAL %d %d Kbits)\n  ", STORAGESIZE, STORAGESIZE / 1024);
#endif

  return (STORAGESIZE);
}

class Tahead {
public:
  Tahead(void) {
    reinit();
#ifdef TAHEAD_PRINTSIZE
    TAHEAD_predictorsize();
#endif
  }

#define NTAHEAD_NHIST 18
  int mm[NTAHEAD_NHIST + 1];

  int TAHEAD_getTableSize(int i) {
    if (TAHEAD_SHARED && i >= 1 && i <= 6) {
      return (1 << (TAHEAD_LOGG + 1)) * TAHEAD_ASSOC;
    } else {
      return (1 << TAHEAD_LOGG) * TAHEAD_ASSOC;
    }
  }

  TAHEAD_gentry &get_TAHEAD_gtable_entry(int i, int j) {
    int idx = j % TAHEAD_getTableSize(i);
    return TAHEAD_gtable[i][idx];
  }

  TAHEAD_bentry &get_TAHEAD_btable_entry(int j) {
    int idx = j % (1 << TAHEAD_LOGB);
    return TAHEAD_btable[idx];
  }

  void reinit() {
    if ((TAHEAD_AHEAD != 0) && (TAHEAD_AHEAD != 2)) {
      printf("Sorry the simulator does not support this TAHEAD_AHEAD distance\n");
      exit(1);
    }
    if ((TAHEAD_LOGASSOC != 1) || (TAHEAD_PSK == 0)) {
#if (TAHEAD_REPSK == 1)

      printf("Sorry TAHEAD_REPSK only with associativity 2 and TAHEAD_PSK activated\n");
      exit(1);

#endif
    }

#ifdef TAHEAD_OPTGEOHIST
    mm[1] = TAHEAD_MINHIST;

    for (int i = 2; i <= NTAHEAD_NHIST; i++) {
      mm[i] = (int)(((double)TAHEAD_MINHIST
                     * pow((double)(TAHEAD_MAXHIST) / (double)TAHEAD_MINHIST, (double)(i - 1) / (double)((NTAHEAD_NHIST - 1))))
                    + 0.5);
    }
    for (int i = 2; i <= NTAHEAD_NHIST; i++) {
      if (mm[i] <= mm[i - 1] + 1) {
        mm[i] = mm[i - 1] + 1;
      }
    }
    int PT = 1;
    for (int i = 1; i <= 3; i += 2) {
      TAHEAD_m[PT] = mm[i];
      PT++;
    }

    for (int i = 5; i <= 14; i++)

    {
      TAHEAD_m[PT] = mm[i];
      PT++;
    }
    PT = TAHEAD_NHIST;

    for (int i = NTAHEAD_NHIST; i > 14; i -= 2) {
      TAHEAD_m[PT] = mm[i];
      PT--;
    }

#else
    TAHEAD_m[1] = TAHEAD_MINHIST;

    for (int i = 2; i <= TAHEAD_NHIST; i++) {
      TAHEAD_m[i] = (int)(((double)TAHEAD_MINHIST
                           * pow((double)(TAHEAD_MAXHIST) / (double)TAHEAD_MINHIST, (double)(i - 1) / (double)((TAHEAD_NHIST - 1))))
                          + 0.5);
    }
    for (int i = 3; i <= TAHEAD_NHIST; i++) {
      if (TAHEAD_m[i] <= TAHEAD_m[i - 1]) {
        TAHEAD_m[i] = TAHEAD_m[i - 1] + 1;
      }
    }
#endif
    if ((TAHEAD_AHEAD != 0) & (TAHEAD_AHEAD != 2)) {
      exit(1);  // prediction is considered to be done in 1 cycle or 3 cycles
    }
    for (int i = 1; i <= TAHEAD_NHIST; i++) {
      TAHEAD_m[i] -= TAHEAD_AHEAD;
    }

#ifdef TAHEAD_ADJACENTTABLE
    if (TAHEAD_LOGASSOC == 0) {
      //  if there is some associativity: no need for this
      for (int i = 2; i <= TAHEAD_NHIST; i += 2)

      {
        TAHEAD_m[i] = TAHEAD_m[i - 1] + ((TAHEAD_m[i] - TAHEAD_m[i - 1]) / 2);

        if (TAHEAD_m[i] == TAHEAD_m[i - 1]) {
          TAHEAD_m[i]++;
        }
      }
    }

#endif
    for (int i = 1; i <= TAHEAD_NHIST; i++) {
      TAHEAD_m[i] <<= 2;
    }
    // 4 bits per block

    for (int i = 1; i <= TAHEAD_NHIST; i++) {
      printf("%d ", TAHEAD_m[i]);
    }
    printf("\n");
#ifndef TAHEAD_INTERLEAVED
    if (TAHEAD_SHARED) {
      /* tailored for 14 tables */
      for (int i = 1; i <= 8; i++) {
        TAHEAD_gtable[i] = new TAHEAD_gentry[(1 << (TAHEAD_LOGG + (i <= 6))) * TAHEAD_ASSOC];
      }
      for (int i = 9; i <= 14; i++) {
        TAHEAD_gtable[i] = TAHEAD_gtable[i - 8];
      }
    }

    else {
      for (int i = 1; i <= TAHEAD_NHIST; i++) {
        TAHEAD_gtable[i] = new TAHEAD_gentry[(1 << (TAHEAD_LOGG)) * TAHEAD_ASSOC];
      }
    }
#else
    TAHEAD_gtable[1] = new TAHEAD_gentry[(1 << (TAHEAD_LOGG)) * TAHEAD_ASSOC * TAHEAD_NHIST];

    for (int i = 2; i <= TAHEAD_NHIST; i++) {
      TAHEAD_gtable[i] = TAHEAD_gtable[1];
    }

#endif

    TAHEAD_btable = new TAHEAD_bentry[1 << TAHEAD_LOGB];
    for (int i = 1; i <= TAHEAD_NHIST; i++) {
      tahead_ch_i[i].init(TAHEAD_m[i], 25 + (2 * ((i - 1) / 2) % 4), i - 1);
      TAHEAD_ch_t[0][i].init(tahead_ch_i[i].OLENGTH, 13, i);
      TAHEAD_ch_t[1][i].init(tahead_ch_i[i].OLENGTH, 11, i + 2);
    }

    TAHEAD_Seed = 0;

    TAHEAD_TICK  = 0;
    TAHEAD_phist = 0;
    TAHEAD_Seed  = 0;

    for (int i = 0; i < TAHEAD_HISTBUFFERLENGTH; i++) {
      TAHEAD_ghist[0] = 0;
    }
    TAHEAD_ptghist = 0;

    TAHEAD_updatethreshold = 23;

#ifdef TAHEAD_SCMEDIUM

    for (int j = 0; j < ((1 << TAHEAD_LOGBNB) - 1); j++) {
      if (!(j & 1)) {
        TAHEAD_BBIAS[j] = -1;
      }
    }
    for (int j = 0; j < ((1 << TAHEAD_LOGFNB) - 1); j++) {
      if (!(j & 1)) {
        TAHEAD_FBIAS[j] = -1;
      }
    }
    for (int j = 0; j < ((1 << TAHEAD_LOGINB) - 1); j++) {
      if (!(j & 1)) {
        TAHEAD_IBIAS[j] = -1;
      }
    }
    for (int j = 0; j < ((1 << TAHEAD_LOGINB) - 1); j++) {
      if (!(j & 1)) {
        TAHEAD_IIBIAS[j] = -1;
      }
    }

#endif

    for (int j = 0; j < (1 << TAHEAD_LOGBIAS); j++) {
      switch (j & 3) {
        case 0: TAHEAD_BiasPCLMAP[j] = -8; break;
        case 1: TAHEAD_BiasPCLMAP[j] = 7; break;
        case 2: TAHEAD_BiasPCLMAP[j] = -32; break;
        case 3: TAHEAD_BiasPCLMAP[j] = 31; break;
      }
    }

    TAHEAD_TICK = 0;

    TAHEAD_ptghist = 0;
    TAHEAD_phist   = 0;
#ifdef TAHEAD_RANDINIT
    if (TAHEAD_NHIST == 14) {
      for (int i = 1; i <= ((TAHEAD_SHARED) ? 8 : 14); i++) {
        for (int j = 0; j < TAHEAD_ASSOC * (1 << (TAHEAD_LOGG + (TAHEAD_SHARED ? (i <= 6) : 0))); j++) {
          int idx                           = j % TAHEAD_getTableSize(i);
          get_TAHEAD_gtable_entry(i, idx).u = random() & ((1 << TAHEAD_UWIDTH) - 1);

          get_TAHEAD_gtable_entry(i, idx).ctr = (random() & 7) - 4;
        }
      }
    }

    else {
      for (int i = 1; i <= TAHEAD_NHIST; i++) {
        for (int j = 0; j < TAHEAD_ASSOC * (1 << TAHEAD_LOGG); j++) {
          int idx                             = j % TAHEAD_getTableSize(i);
          get_TAHEAD_gtable_entry(i, idx).u   = random() & ((1 << TAHEAD_UWIDTH) - 1);
          get_TAHEAD_gtable_entry(i, idx).ctr = (random() & 7) - 4;
        }
      }
    }

    TAHEAD_TICK  = TAHEAD_BORNTICK / 2;
    TAHEAD_TICKH = TAHEAD_BORNTICK / 2;
    for (int i = 0; i < (1 << TAHEAD_LOGB); i++) {
      get_TAHEAD_btable_entry(i).pred = random() & 1;
      get_TAHEAD_btable_entry(i).hyst = random() & 3;
    }
    TAHEAD_updatethreshold = 23;
#ifdef TAHEAD_SCMEDIUM
    for (int j = 0; j < ((1 << TAHEAD_LOGBNB) - 1); j++) {
      TAHEAD_BBIAS[j] = -(1 << (TAHEAD_PERCWIDTH - 1)) + (random() & ((1 << TAHEAD_PERCWIDTH) - 1));
    }
    for (int j = 0; j < ((1 << TAHEAD_LOGFNB) - 1); j++) {
      TAHEAD_FBIAS[j] = -(1 << (TAHEAD_PERCWIDTH - 1)) + (random() & ((1 << TAHEAD_PERCWIDTH) - 1));
    }
    for (int j = 0; j < ((1 << TAHEAD_LOGINB) - 1); j++) {
      TAHEAD_IBIAS[j] = -(1 << (TAHEAD_PERCWIDTH - 1)) + (random() & ((1 << TAHEAD_PERCWIDTH) - 1));
    }
    for (int j = 0; j < ((1 << TAHEAD_LOGINB) - 1); j++) {
      TAHEAD_IIBIAS[j] = -(1 << (TAHEAD_PERCWIDTH - 1)) + (random() & ((1 << TAHEAD_PERCWIDTH) - 1));
    }

#endif
    for (int j = 0; j < (1 << TAHEAD_LOGBIAS); j++) {
      TAHEAD_BiasPCLMAP[j] = -(1 << (TAHEAD_PERCWIDTH - 1)) + (random() & ((1 << TAHEAD_PERCWIDTH) - 1));
      TAHEAD_BiasPC[j]     = -(1 << (TAHEAD_PERCWIDTH - 1)) + (random() & ((1 << TAHEAD_PERCWIDTH) - 1));
    }

#endif
  }

  // index function for the bimodal table

  int bindex(uint64_t PC) { return ((PC ^ (PC >> TAHEAD_LOGB)) & ((1 << (TAHEAD_LOGB)) - 1)); }
  // the index functions for the tagged tables uses path history as in the OBIAS predictor
  // F serves to mix path history: not very important impact

  int F(long long A, int size, int bank) {
    int A1, A2;
    A  = A & ((1 << size) - 1);
    A1 = (A & ((1 << TAHEAD_LOGG) - 1));
    A2 = (A >> TAHEAD_LOGG);
    if (bank < TAHEAD_LOGG) {
      A2 = ((A2 << bank) & ((1 << TAHEAD_LOGG) - 1)) ^ (A2 >> (TAHEAD_LOGG - bank));
    }
    A = A1 ^ A2;
    if (bank < TAHEAD_LOGG) {
      A = ((A << bank) & ((1 << TAHEAD_LOGG) - 1)) ^ (A >> (TAHEAD_LOGG - bank));
    }
    //  return(0);
    return (A);
  }

  // gindex computes a full hash of PC, TAHEAD_ghist and TAHEAD_phist
  uint gindex(unsigned int PC, int bank, long long hist, TAHEAD_folded_history *ptahead_ch_i) {
    uint index;
    int  logg  = TAHEAD_LOGG + /* TAHEAD_SHARED+*/ (TAHEAD_SHARED & (bank <= 1));
    uint M     = (TAHEAD_m[bank] > TAHEAD_PHISTWIDTH) ? TAHEAD_PHISTWIDTH : TAHEAD_m[bank];
    index      = PC ^ (PC >> (abs(logg - bank) + 1)) ^ ptahead_ch_i[bank].comp ^ F(hist, M, bank);
    uint32_t X = (index ^ (index >> logg) ^ (index >> 2 * logg)) & ((1 << logg) - 1);
#ifdef TAHEAD_INTERLEAVED
    if (bank == 1) {
      BANK1 = index % TAHEAD_NHIST;
    }
#endif

    return (X);
  }

  //  tag computation
  uint16_t gtag(unsigned int PC, int bank, TAHEAD_folded_history *ch0, TAHEAD_folded_history *ch1) {
    int tag = PC ^ (PC >> 2);
    int M   = (TAHEAD_m[bank] > TAHEAD_PHISTWIDTH) ? TAHEAD_PHISTWIDTH : TAHEAD_m[bank];
    tag     = (tag >> 1) ^ ((tag & 1) << 10) ^ F(TAHEAD_phist, M, bank);
    tag ^= ch0[bank].comp ^ (ch1[bank].comp << 1);
    tag ^= tag >> TAHEAD_TBITS;
    tag ^= (tag >> (TAHEAD_TBITS - 2));

    return tag & ((1 << TAHEAD_TBITS) - 1);
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
    TAHEAD_BIM      = (get_TAHEAD_btable_entry(TAHEAD_BI).pred) ? (get_TAHEAD_btable_entry(TAHEAD_BI >> TAHEAD_HYSTSHIFT).hyst)
                                                                : -1 - (get_TAHEAD_btable_entry(TAHEAD_BI >> TAHEAD_HYSTSHIFT).hyst);
    TAHEAD_TAGECONF = 3 * (get_TAHEAD_btable_entry(TAHEAD_BI >> TAHEAD_HYSTSHIFT).hyst != 0);

    return (get_TAHEAD_btable_entry(TAHEAD_BI).pred != 0);
  }

  void baseupdate(bool Taken) {
    int8_t inter = TAHEAD_BIM;
    ctrupdate(inter, Taken, TAHEAD_BIMWIDTH);
    get_TAHEAD_btable_entry(TAHEAD_BI).pred                     = (inter >= 0);
    get_TAHEAD_btable_entry(TAHEAD_BI >> TAHEAD_HYSTSHIFT).hyst = (inter >= 0) ? inter : -inter - 1;
  };
  uint32_t MYRANDOM() {
    // This pseudo-random function: just to be sure that the simulator is deterministic
    //  results are within +- 0.002 MPKI in average with some larger difference on individual benchmarks
    TAHEAD_Seed++;
    TAHEAD_Seed += TAHEAD_phist;
    TAHEAD_Seed = (TAHEAD_Seed >> 21) + (TAHEAD_Seed << 11);
    TAHEAD_Seed += TAHEAD_ptghist;
    TAHEAD_Seed = (TAHEAD_Seed >> 10) + (TAHEAD_Seed << 22);
    TAHEAD_Seed += TAHEAD_GTAG[4];
    return (TAHEAD_Seed);
  };

  //  TAGE PREDICTION: same code at fetch or retire time but the index and tags must recomputed
  void Tagepred(uint64_t PC) {
    TAHEAD_HitBank    = 0;
    TAHEAD_AltBank    = 0;
    TAHEAD_HCpredBank = 0;
    if (TAHEAD_Numero == 0) {
      for (int i = 1; i <= TAHEAD_NHIST; i++) {
        TAHEAD_AHGI[TAHEAD_NPRED % 10][i]   = gindex(PC, i, TAHEAD_phist, tahead_ch_i);
        TAHEAD_AHGTAG[TAHEAD_NPRED % 10][i] = gtag(PC, i, TAHEAD_ch_t[0], TAHEAD_ch_t[1]);
      }
      if (TAHEAD_SHARED) {
        int X = TAHEAD_AHGI[TAHEAD_NPRED % 10][1] & 1;
        for (int i = 2; i <= 6; i++) {
          TAHEAD_AHGI[TAHEAD_NPRED % 10][i] <<= 1;
          TAHEAD_AHGI[TAHEAD_NPRED % 10][i] ^= X;
        }
        for (int i = 9; i <= 14; i++) {
          TAHEAD_AHGI[TAHEAD_NPRED % 10][i] <<= 1;
          TAHEAD_AHGI[TAHEAD_NPRED % 10][i] ^= X ^ 1;
        }
      }
#ifdef TAHEAD_INTERLEAVED
#ifndef TAHEAD_ADJACENTTABLE
      for (int i = 1; i <= TAHEAD_NHIST; i++) {
        TAHEAD_AHGI[TAHEAD_NPRED % 10][i] += ((BANK1 + i) % TAHEAD_NHIST) * (1 << (TAHEAD_LOGG));
      }
#else
      for (int i = 2; i <= TAHEAD_NHIST; i += 2) {
        TAHEAD_AHGI[TAHEAD_NPRED % 10][i] = TAHEAD_AHGI[TAHEAD_NPRED % 10][i - 1];
      }
      for (int i = 1; i <= TAHEAD_NHIST; i++) {
        TAHEAD_AHGI[TAHEAD_NPRED % 10][i]
            += ((BANK1 + ((i - 1) / 2)) % (TAHEAD_NHIST / 2)) * (1 << (TAHEAD_LOGG + 1)) + ((i & 1) << TAHEAD_LOGG);
      }

#endif
#endif
    }
    int AHEADTAG = (TAHEAD_AHEAD > 0) ? TAHEAD_AHEAD - 1 : TAHEAD_AHEAD;
    // assumes that the tag is used one cycle later than the index if TAHEAD_AHEAD pipelining is used.

    TAHEAD_BI = (TAHEAD_PCBLOCK ^ ((TAHEAD_Numero & (TAHEAD_NBREADPERTABLE - 1)) << (TAHEAD_LOGB - 2))) & ((1 << TAHEAD_LOGB) - 1);

    // For TAHEAD_AHEAD, one considers that the bimodal prediction is  obtained during the last cycle
    for (int i = 1; i <= TAHEAD_NHIST; i++) {
#if (TAHEAD_AHEAD != 0)
      {
        TAHEAD_GI[i] = TAHEAD_AHGI[(TAHEAD_NPRED - TAHEAD_AHEAD) % 10][i]
                       ^ (((TAHEAD_GH ^ TAHEAD_Numero ^ TAHEAD_BI ^ (TAHEAD_PCBLOCK >> 3)) & (TAHEAD_READWIDTHAHEAD - 1))
                          << (TAHEAD_LOGG - TAHEAD_LOGASSOC - 4));
        // some bits are hashed on  values that are unknown at prediction read time: assumes READWITHTAHEAD reads at a time

        TAHEAD_GI[i] *= TAHEAD_ASSOC;
        TAHEAD_GTAG[i] = TAHEAD_AHGTAG[(TAHEAD_NPRED - AHEADTAG) % 10][i]

                         ^ (((TAHEAD_GH ^ (TAHEAD_GH >> 1)) & (TAHEAD_TAGCHECKAHEAD - 1)));

        ;

        // TAHEAD_TAGCHECKAHEAD reads per read entry
      }

#else
      {
        TAHEAD_GI[i] = TAHEAD_AHGI[(TAHEAD_NPRED - TAHEAD_AHEAD) % 10][i]
                       ^ ((TAHEAD_Numero & (TAHEAD_NBREADPERTABLE - 1)) << (TAHEAD_LOGG - TAHEAD_LOGASSOC - 2));
        TAHEAD_GI[i] *= TAHEAD_ASSOC;
        TAHEAD_GTAG[i] = TAHEAD_AHGTAG[(TAHEAD_NPRED - AHEADTAG) % 10][i] ^ (TAHEAD_Numero);
      }
#endif
    }
#ifndef TAHEAD_INTERLEAVED
#ifdef TAHEAD_ADJACENTTABLE
    for (int i = 2; i <= TAHEAD_NHIST; i += 2) {
      TAHEAD_GI[i] = TAHEAD_GI[i - 1];
    }

#endif
#endif
    for (int i = 1; i <= TAHEAD_NHIST; i++) {
      for (int j = 0; j < TAHEAD_ASSOC; j++) {
        TAHEAD_GGI[j][i] = TAHEAD_GI[i];
      }
      if (TAHEAD_PSK == 1) {
        if (TAHEAD_AHEAD == 0) {
          for (int j = 1; j < TAHEAD_ASSOC; j++) {
            TAHEAD_GGI[j][i] ^= ((TAHEAD_GTAG[i] >> (3 + 2 * j)) & 0x3) << (TAHEAD_LOGG - 3);
          }
        } else {
          for (int j = 1; j < TAHEAD_ASSOC; j++) {
            TAHEAD_GGI[j][i] ^= ((TAHEAD_GTAG[i] >> (3 + 2 * j)) & (TAHEAD_READWIDTHAHEAD - 1)) << (TAHEAD_LOGG - 5);
          }
        }
      }

      // works for TAHEAD_AHEAD also if TAHEAD_READWIDTHAHEAD <= 16
    }

    TAHEAD_alttaken         = getbim();
    TAHEAD_HCpred           = TAHEAD_alttaken;
    TAHEAD_tage_pred        = TAHEAD_alttaken;
    TAHEAD_LongestMatchPred = TAHEAD_alttaken;

    // Look for the bank with longest matching history
    for (int i = TAHEAD_NHIST; i > 0; i--) {
      for (int j = 0; j < TAHEAD_ASSOC; j++) {
        // int idx = (TAHEAD_GGI[j][i] + j)%TAHEAD_getTableSize(i);
        if (get_TAHEAD_gtable_entry(i, TAHEAD_GGI[j][i] + j).tag == TAHEAD_GTAG[i]) {
          TAHEAD_HitBank  = i;
          TAHEAD_HitAssoc = j;

          TAHEAD_LongestMatchPred
              = (get_TAHEAD_gtable_entry(TAHEAD_HitBank, TAHEAD_GGI[TAHEAD_HitAssoc][TAHEAD_HitBank] + TAHEAD_HitAssoc).ctr >= 0);
          TAHEAD_TAGECONF
              = (abs(2 * get_TAHEAD_gtable_entry(TAHEAD_HitBank, TAHEAD_GGI[TAHEAD_HitAssoc][TAHEAD_HitBank] + TAHEAD_HitAssoc).ctr
                     + 1))
                >> 1;

          break;
        }
      }
      if (TAHEAD_HitBank > 0) {
        break;
      }
    }
    // should be noted that when TAHEAD_LongestMatchPred is not low conf then TAHEAD_alttaken is the 2nd not-low conf:  not a
    // critical path, needed only on update.
    for (int i = TAHEAD_HitBank - 1; i > 0; i--) {
      for (int j = 0; j < TAHEAD_ASSOC; j++) {
        if (get_TAHEAD_gtable_entry(i, TAHEAD_GGI[j][i] + j).tag == TAHEAD_GTAG[i]) {
          // if (abs (2 * TAHEAD_gtable[i][TAHEAD_GGI[j][i] + j].ctr + 1) != 1)
          // slightly better to pick alternate prediction as not low confidence
          {
            TAHEAD_AltAssoc = j;
            TAHEAD_AltBank  = i;
            break;
          }
        }
      }
      if (TAHEAD_AltBank > 0) {
        break;
      }
    }
    if (TAHEAD_HitBank > 0) {
      if (abs(2 * get_TAHEAD_gtable_entry(TAHEAD_HitBank, TAHEAD_GGI[TAHEAD_HitAssoc][TAHEAD_HitBank] + TAHEAD_HitAssoc).ctr + 1)
          == 1) {
        for (int i = TAHEAD_HitBank - 1; i > 0; i--) {
          for (int j = 0; j < TAHEAD_ASSOC; j++) {
            if (get_TAHEAD_gtable_entry(i, TAHEAD_GGI[j][i] + j).tag == TAHEAD_GTAG[i]) {
              if (abs(2 * get_TAHEAD_gtable_entry(i, TAHEAD_GGI[j][i] + j).ctr + 1) != 1)
              // slightly better to pick alternate prediction as not low confidence
              {
                TAHEAD_HCpredBank = i;

                TAHEAD_HCpredAssoc = j;
                TAHEAD_HCpred      = (get_TAHEAD_gtable_entry(i, TAHEAD_GGI[j][i] + j).ctr >= 0);

                break;
              }
            }
          }
          if (TAHEAD_HCpredBank > 0) {
            break;
          }
        }
      }

      else {
        TAHEAD_HCpredBank  = TAHEAD_HitBank;
        TAHEAD_HCpredAssoc = TAHEAD_HitAssoc;
        TAHEAD_HCpred      = TAHEAD_LongestMatchPred;
      }
    }

    // computes the prediction and the alternate prediction

    if (TAHEAD_HitBank > 0) {
      if (TAHEAD_AltBank > 0) {
        TAHEAD_alttaken
            = (get_TAHEAD_gtable_entry(TAHEAD_AltBank, TAHEAD_GGI[TAHEAD_AltAssoc][TAHEAD_AltBank] + TAHEAD_AltAssoc).ctr >= 0);
      }

#ifndef TAHEAD_SC
      // if the entry is recognized as a newly allocated entry and
      // USE_ALT_ON_NA is positive  use the alternate prediction
      bool Huse_alt_on_na = (TAHEAD_use_alt_on_na >= 0);

      if ((!Huse_alt_on_na)
          || (abs(2 * get_TAHEAD_gtable_entry(TAHEAD_HitBank, TAHEAD_GGI[TAHEAD_HitAssoc][TAHEAD_HitBank] + TAHEAD_HitAssoc).ctr
                  + 1)
              > 1)) {
        TAHEAD_tage_pred = TAHEAD_LongestMatchPred;
      } else {
        TAHEAD_tage_pred = TAHEAD_HCpred;
      }

#else
      TAHEAD_tage_pred = TAHEAD_LongestMatchPred;
#endif
    }
  }

  // compute the prediction

  void fetchBoundaryEnd() {
#ifdef TAHEAD_DELAY_UPDATE
    // TAHEAD_Numero = 0;
#endif
  }

  bool getPrediction(uint64_t PCBRANCH) {
    (void)PCBRANCH;

    uint64_t PC = TAHEAD_PCBLOCK ^ (TAHEAD_Numero << 5);

    // computes the TAGE table addresses and the partial tags
    Tagepred(PC);
    TAHEAD_pred_taken = TAHEAD_tage_pred;
    TAHEAD_predSC     = TAHEAD_pred_taken;
    TAHEAD_predTSC    = TAHEAD_pred_taken;

    // printf("pc:%lx Num:%lx ptaken:%d\n", PC, TAHEAD_Numero, TAHEAD_pred_taken);
#ifdef TAHEAD_DELAY_UPDATE
    TAHEAD_Numero++;
#endif

#ifndef TAHEAD_SC
#ifdef LMP
    return (TAHEAD_LongestMatchPred);
#endif
    return TAHEAD_pred_taken;
#endif
    if (TAHEAD_AHEAD) {
      PC = TAHEAD_PrevPCBLOCK ^ (TAHEAD_Numero << 5) ^ (TAHEAD_PrevNumero << 5) ^ ((TAHEAD_BI & 3) << 5);
    }

    // Let us  compute the TAHEAD_SC prediction
    TAHEAD_SUMSC = 0;
////// These extra counters seem to bring a marginal  gain of 0.006 MPKI  when only pure TAHEAD_SC, not useful when other info
#ifndef TAHEAD_SCMEDIUM
    TAHEAD_SUMSC += TAHEAD_incval(TAHEAD_BiasGEN);
    TAHEAD_SUMSC += TAHEAD_incval(TAHEAD_BiasLM[TAHEAD_LongestMatchPred]);
    TAHEAD_SUMSC += TAHEAD_incval(TAHEAD_BiasAP[TAHEAD_HCpred]);
#endif
    //////

    TAHEAD_SUMSC += TAHEAD_incval(TAHEAD_BiasLMAP[TAHEAD_INDBIASLMAP]);
    // x 2: a little bit better
    TAHEAD_SUMSC += 2 * TAHEAD_incval(TAHEAD_BiasPC[TAHEAD_INDBIASPC]);
    TAHEAD_SUMSC += TAHEAD_incval(TAHEAD_BiasPCLMAP[TAHEAD_INDBIASPCLMAP]);

    TAHEAD_predTSC = (TAHEAD_SUMSC >= 0);
    // when TAHEAD_predTSC is correct we do not allocate any new entry
#ifdef TAHEAD_SCMEDIUM
    TAHEAD_SUMFULL = 0;
    TAHEAD_SUMFULL += TAHEAD_incval(TAHEAD_IIBIAS[TAHEAD_INDBIASIMLIBR]);
#ifdef TAHEAD_SCFULL
    TAHEAD_SUMFULL += TAHEAD_incval(TAHEAD_FBIAS[TAHEAD_INDBIASFHIST]);
    TAHEAD_SUMFULL += TAHEAD_incval(TAHEAD_BBIAS[TAHEAD_INDBIASBHIST]);
    TAHEAD_SUMFULL += TAHEAD_incval(TAHEAD_IBIAS[TAHEAD_INDBIASIMLITA]);

#endif

    // x 2: a little bit better

    TAHEAD_SUMSC += 2 * TAHEAD_SUMFULL;
#endif
    bool SCPRED = (TAHEAD_SUMSC >= 0);

#ifdef TAHEAD_FORCEONHIGHCONF
    TAHEAD_pred_taken
        = (TAHEAD_TAGECONF != 3) || (abs(TAHEAD_SUMSC) >= TAHEAD_updatethreshold / 2) ? SCPRED : TAHEAD_LongestMatchPred;
#else
    TAHEAD_pred_taken = (TAHEAD_SUMSC >= 0);
#endif

    TAHEAD_predSC = (TAHEAD_SUMSC >= 0);

    return TAHEAD_pred_taken;
  }

  void HistoryUpdate(uint64_t PCBRANCH, Opcode opType, bool taken, uint64_t branchTarget, int &Y, TAHEAD_folded_history *H,
                     TAHEAD_folded_history *G, TAHEAD_folded_history *J) {
    int brtype;

    if ((TAHEAD_Numero == TAHEAD_MAXBR - 1) || (taken)) {
      TAHEAD_GH = (TAHEAD_GH << 2) ^ PCBRANCH;

      uint64_t PC = TAHEAD_PCBLOCK ^ (TAHEAD_Numero << 5);
      TAHEAD_NPRED++;
      TAHEAD_GH <<= 2;
      uint64_t Successor = (taken) ? branchTarget ^ (branchTarget >> 4) : (PCBRANCH + 1) ^ ((PCBRANCH + 1) >> 4);
      TAHEAD_GH ^= ((TAHEAD_Numero) ^ Successor);
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
#ifdef TAHEAD_SCMEDIUM
      TAHEAD_PrevF_TaIMLI = TAHEAD_F_TaIMLI;
      TAHEAD_PrevF_BrIMLI = TAHEAD_F_BrIMLI;
      TAHEAD_PrevBHIST    = TAHEAD_BHIST;
      TAHEAD_PrevFHIST    = TAHEAD_FHIST;
      if (taken) {
        if (branchTarget > PCBRANCH) {
          TAHEAD_FHIST = (TAHEAD_FHIST << 3) ^ (branchTarget >> 2) ^ (PCBRANCH >> 1);
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
            if (((branchTarget & 65535) >> 6) == TAHEAD_LastBack) {
              if (TAHEAD_TaIMLI < ((1 << TAHEAD_LOGBIAS) - 1)) {
                TAHEAD_TaIMLI++;
              }
            } else {
              TAHEAD_BBHIST = (TAHEAD_BBHIST << 1) ^ TAHEAD_LastBack;
              TAHEAD_TaIMLI = 0;
            }
            if (((PCBRANCH & 65535) >> 6) == TAHEAD_LastBackPC)

            {
              if (TAHEAD_BrIMLI < ((1 << TAHEAD_LOGBIAS) - 1)) {
                TAHEAD_BrIMLI++;
              }
            } else {
              TAHEAD_BBHIST = (TAHEAD_BBHIST << 1) ^ TAHEAD_LastBackPC;
              TAHEAD_BrIMLI = 0;
            }
            TAHEAD_LastBack   = (branchTarget & 65535) >> 6;
            TAHEAD_LastBackPC = (PCBRANCH & 65535) >> 6;
          }
        }
      }
#endif
      if (TAHEAD_AHEAD) {
        // to hash with TAHEAD_Numero
        TAHEAD_PrevNumero = (TAHEAD_Numero & 1) << 1;
        TAHEAD_PrevNumero ^= (TAHEAD_Numero >> 1);
      }

      TAHEAD_Numero <<= 1;
      TAHEAD_Numero += taken;

      int T = ((PC ^ (PC >> 2))) ^ TAHEAD_Numero ^ (branchTarget >> 3);

      int PATH     = PC ^ (PC >> 2) ^ (PC >> 4) ^ (branchTarget) ^ (TAHEAD_Numero << 3);
      TAHEAD_phist = (TAHEAD_phist << 4) ^ PATH;
      TAHEAD_phist = (TAHEAD_phist & ((1 << 27) - 1));

      for (int t = 0; t < 4; t++) {
        int DIR = (T & 1);
        T >>= 1;

        PATH >>= 1;
        Y--;
        TAHEAD_ghist[Y & (TAHEAD_HISTBUFFERLENGTH - 1)] = DIR;
        for (int i = 1; i <= TAHEAD_NHIST; i++) {
          H[i].update(TAHEAD_ghist, Y);
          G[i].update(TAHEAD_ghist, Y);
          J[i].update(TAHEAD_ghist, Y);
        }
      }

      TAHEAD_Numero = 0;

      TAHEAD_PrevPCBLOCK = TAHEAD_PCBLOCK;
      TAHEAD_PCBLOCK     = (taken) ? branchTarget : PCBRANCH + 1;
      TAHEAD_PCBLOCK     = TAHEAD_PCBLOCK ^ (TAHEAD_PCBLOCK >> 4);

    } else {
      TAHEAD_Numero++;
    }
    TAHEAD_BHIST    = (TAHEAD_BrIMLI == 0) ? TAHEAD_BBHIST : ((TAHEAD_BBHIST & 15) + (TAHEAD_BrIMLI << 6)) ^ (TAHEAD_BrIMLI >> 4);
    TAHEAD_F_TaIMLI = (TAHEAD_TaIMLI == 0) || (TAHEAD_BrIMLI == TAHEAD_TaIMLI) ? (TAHEAD_GH) : TAHEAD_TaIMLI;
    TAHEAD_F_BrIMLI = (TAHEAD_BrIMLI == 0) ? (TAHEAD_phist) : TAHEAD_BrIMLI;

    if (TAHEAD_AHEAD == 0) {
      TAHEAD_PrevF_TaIMLI = TAHEAD_F_TaIMLI;
      TAHEAD_PrevF_BrIMLI = TAHEAD_F_BrIMLI;
      TAHEAD_PrevBHIST    = TAHEAD_BHIST;
      TAHEAD_PrevFHIST    = TAHEAD_FHIST;
    }
  }

  // END UPDATE  HISTORIES

  // Tahead UPDATE

  void updatePredictor(uint64_t PCBRANCH, Opcode opType, bool resolveDir, bool predDir, uint64_t branchTarget) {
    // uint64_t PC = TAHEAD_PCBLOCK ^ (TAHEAD_Numero << 5);
    //
    // if (TAHEAD_AHEAD) {
    //   PC = TAHEAD_PrevPCBLOCK ^ (TAHEAD_Numero << 5) ^ (TAHEAD_PrevNumero << 5) ^ ((TAHEAD_BI & 3) << 5);
    // }
    // bool DONE = false;
#ifndef TAHEAD_DELAY_UPDATE
    (void)predDir;
#endif
#ifdef TAHEAD_SC
    bool SCPRED = (TAHEAD_SUMSC >= 0);
    if ((SCPRED != resolveDir) || ((abs(TAHEAD_SUMSC) < TAHEAD_updatethreshold)))

    {
      if (SCPRED != resolveDir) {
        if (TAHEAD_updatethreshold < (1 << (TAHEAD_WIDTHRES)) - 1) {
          TAHEAD_updatethreshold += 1;
        }
      } else {
        if (TAHEAD_updatethreshold > 0) {
          TAHEAD_updatethreshold -= 1;
        }
      }

      ctrupdate(TAHEAD_BiasGEN, resolveDir, TAHEAD_PERCWIDTH);
      ctrupdate(TAHEAD_BiasAP[TAHEAD_HCpred], resolveDir, TAHEAD_PERCWIDTH);
      ctrupdate(TAHEAD_BiasLM[TAHEAD_LongestMatchPred], resolveDir, TAHEAD_PERCWIDTH);
      ctrupdate(TAHEAD_BiasLMAP[TAHEAD_INDBIASLMAP], resolveDir, TAHEAD_PERCWIDTH);

      ctrupdate(TAHEAD_BiasPC[TAHEAD_INDBIASPC], resolveDir, TAHEAD_PERCWIDTH);
      ctrupdate(TAHEAD_BiasPCLMAP[TAHEAD_INDBIASPCLMAP], resolveDir, TAHEAD_PERCWIDTH);

#ifdef TAHEAD_SCMEDIUM

      ctrupdate(TAHEAD_IBIAS[TAHEAD_INDBIASIMLITA], resolveDir, TAHEAD_PERCWIDTH);
      ctrupdate(TAHEAD_IIBIAS[TAHEAD_INDBIASIMLIBR], resolveDir, TAHEAD_PERCWIDTH);
      ctrupdate(TAHEAD_BBIAS[TAHEAD_INDBIASBHIST], resolveDir, TAHEAD_PERCWIDTH);
      ctrupdate(TAHEAD_FBIAS[TAHEAD_INDBIASFHIST], resolveDir, TAHEAD_PERCWIDTH);

#endif
    }

#endif

    // TAGE UPDATE
    bool ALLOC = (TAHEAD_HitBank < TAHEAD_NHIST);
    ALLOC &= (TAHEAD_LongestMatchPred != resolveDir);
    ALLOC &= (TAHEAD_predTSC != resolveDir);
    if (TAHEAD_HitBank > 0) {
      if ((TAHEAD_TAGECONF == 0) || ((MYRANDOM() & 3) == 0)) {
        ctrupdate(TAHEAD_CountLowConf, (TAHEAD_TAGECONF == 0), 7);
      }
    }

    //////////////////////////////////////////////////

    if (TAHEAD_HitBank > 0) {
      bool PseudoNewAlloc
          = (abs(2 * get_TAHEAD_gtable_entry(TAHEAD_HitBank, TAHEAD_GGI[TAHEAD_HitAssoc][TAHEAD_HitBank] + TAHEAD_HitAssoc).ctr + 1)
             <= 1);
      // an entry is considered as newly allocated if its prediction counter is weak

      if (PseudoNewAlloc) {
#ifndef TAHEAD_SC
        if (TAHEAD_LongestMatchPred == resolveDir) {
          ALLOC = false;
        }

        if (TAHEAD_LongestMatchPred != TAHEAD_HCpred) {
          ctrupdate(TAHEAD_use_alt_on_na, (TAHEAD_HCpred == resolveDir), TAHEAD_ALTWIDTH);
          // pure TAGE only
        }
#endif
      }
    }

/////////////////////////
#ifdef TAHEAD_FILTERALLOCATION
    // filter allocations: all of this is done at update, not on the critical path
    //  try to evaluate if the misprediction rate is above 1/9th

    if ((TAHEAD_tage_pred != resolveDir) || ((MYRANDOM() & 31) < 4)) {
      ctrupdate(TAHEAD_CountMiss11, (TAHEAD_tage_pred != resolveDir), 8);
    }

    if (TAHEAD_HitBank > 0) {
      bool PseudoNewAlloc = (TAHEAD_TAGECONF == 0);

      if (PseudoNewAlloc) {
        // Here we count correct/wrong weak counters to guide allocation
        for (int i = TAHEAD_HitBank / 4; i <= TAHEAD_NHIST / 4; i++) {
          ctrupdate(TAHEAD_COUNT50[i],
                    (resolveDir == TAHEAD_LongestMatchPred),
                    7);  // more or less than 50 % good predictions on weak counters
          if ((TAHEAD_LongestMatchPred != resolveDir) || ((MYRANDOM() & 31) > 1)) {
            ctrupdate(TAHEAD_COUNT16_31[i],
                      (resolveDir == TAHEAD_LongestMatchPred),
                      7);  // more or less than 16/31  good predictions on weak counters
          }
        }
      }
    }
    //  when allocating a new entry is unlikely to result in a good prediction, rarely allocate

    if ((TAHEAD_COUNT50[(TAHEAD_HitBank + 1) / 4] < 0)) {
      ALLOC &= ((MYRANDOM() & ((1 << (3)) - 1)) == 0);
    } else
      // the future allocated entry is not that likely to be correct
      if ((TAHEAD_COUNT16_31[(TAHEAD_HitBank + 1) / 4] < 0)) {
        ALLOC &= ((MYRANDOM() & ((1 << 1) - 1)) == 0);
      }
// The benefit is essentially to decrease the number of allocations
#endif

    if (ALLOC) {
      int MaxNALLOC = (TAHEAD_CountMiss11 < 0) + 8 * (TAHEAD_CountLowConf >= 0);
      // this TAHEAD_CountLowConf is not very useful :-)

      int NA      = 0;
      int DEP     = TAHEAD_HitBank + 1;
      int Penalty = 0;
      DEP += ((MYRANDOM() & 1) == 0);
      DEP += ((MYRANDOM() & 3) == 0);
      if (DEP == TAHEAD_HitBank) {
        DEP = TAHEAD_HitBank + 1;
      }

      bool First = true;
#ifdef TAHEAD_FILTERALLOCATION
      bool Test  = false;
#endif

      for (int i = DEP; i <= TAHEAD_NHIST; i++) {
#ifdef TAHEAD_FILTERALLOCATION
        // works because the physical tables are shared
        if (TAHEAD_SHARED) {
          if ((i > 8) & (!Test)) {
            Test = true;

            if ((TAHEAD_CountMiss11 >= 0)) {
              if ((MYRANDOM() & 7) > 0) {
                break;
              }
            }
          }
        }
#endif
        bool done = false;
        uint j    = (MYRANDOM() % TAHEAD_ASSOC);
        {
          bool REP[2]  = {false};
          int  IREP[2] = {0};
          bool MOVE[2] = {false};
          for (int J = 0; J < TAHEAD_ASSOC; J++) {
            j++;
            j       = j % TAHEAD_ASSOC;
            int idx = (TAHEAD_GGI[j][i] + j) % TAHEAD_getTableSize(i);

            if (get_TAHEAD_gtable_entry(i, idx).u == 0) {
              REP[j]  = true;
              IREP[j] = idx;

            }

            else if (TAHEAD_REPSK == 1) {
              if (TAHEAD_AHEAD == 0) {
                IREP[j] = TAHEAD_GGI[j][i] ^ ((((get_TAHEAD_gtable_entry(i, idx).tag >> 5) & 3) << (TAHEAD_LOGG - 3)) + (j ^ 1));
              } else {
                IREP[j] = TAHEAD_GGI[j][i]
                          ^ ((((get_TAHEAD_gtable_entry(i, idx).tag >> 5) & (TAHEAD_READWIDTHAHEAD - 1)) << (TAHEAD_LOGG - 5))
                             + (j ^ 1));
              }

              REP[j] = (get_TAHEAD_gtable_entry(i, IREP[j]).u == 0);

              MOVE[j] = true;
            }

            if (REP[j]) {
              if ((((TAHEAD_UWIDTH == 1)
                    && ((((MYRANDOM() & ((1 << (abs(2 * get_TAHEAD_gtable_entry(i, idx).ctr + 1) >> 1)) - 1)) == 0))))
                   || (TAHEAD_TICKH >= TAHEAD_BORNTICK / 2))
                  || (TAHEAD_UWIDTH == 2)) {
                done = true;
                if (MOVE[j]) {
                  get_TAHEAD_gtable_entry(i, IREP[j]).u   = get_TAHEAD_gtable_entry(i, idx).u;
                  get_TAHEAD_gtable_entry(i, IREP[j]).tag = get_TAHEAD_gtable_entry(i, idx).tag;
                  get_TAHEAD_gtable_entry(i, IREP[j]).ctr = get_TAHEAD_gtable_entry(i, idx).ctr;
                }

                get_TAHEAD_gtable_entry(i, idx).tag = TAHEAD_GTAG[i];
#ifndef TAHEAD_FORCEU
                get_TAHEAD_gtable_entry(i, idx).u = 0;
#else

                get_TAHEAD_gtable_entry(i, idx).u
                    = ((TAHEAD_UWIDTH == 2) || (TAHEAD_TICKH >= TAHEAD_BORNTICK / 2)) & (First ? 1 : 0);
#endif
                get_TAHEAD_gtable_entry(i, idx).ctr = (resolveDir) ? 0 : -1;

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
#ifdef TAHEAD_FORCEU

            for (int jj = 0; jj < TAHEAD_ASSOC; jj++) {
              {
                // some just allocated entries  have been set to useful
                int idxj = (TAHEAD_GGI[jj][i] + jj) % TAHEAD_getTableSize(i);
                if ((MYRANDOM() & ((1 << (1 + TAHEAD_LOGASSOC + TAHEAD_REPSK)) - 1)) == 0) {
                  if (abs(2 * get_TAHEAD_gtable_entry(i, idxj).ctr + 1) == 1) {
                    if (get_TAHEAD_gtable_entry(i, idxj).u == 1) {
                      get_TAHEAD_gtable_entry(i, idxj).u--;
                    }
                  }
                }
                if (TAHEAD_REPSK == 1) {
                  if ((MYRANDOM() & ((1 << (1 + TAHEAD_LOGASSOC + TAHEAD_REPSK)) - 1)) == 0) {
                    if (abs(2 * get_TAHEAD_gtable_entry(i, IREP[jj]).ctr + 1) == 1) {
                      if (get_TAHEAD_gtable_entry(i, IREP[jj]).u == 1) {
                        get_TAHEAD_gtable_entry(i, IREP[jj]).u--;
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

// we set two counts to monitor: "time to reset u" and "almost time reset u": TAHEAD_TICKH is useful only if TAHEAD_UWIDTH =1
#ifndef TAHEAD_PROTECTRECENTALLOCUSEFUL
      TAHEAD_TICKH += Penalty - NA;
      TAHEAD_TICK += Penalty - 2 * NA;
#else
      TAHEAD_TICKH += 2 * Penalty - 3 * NA;
      TAHEAD_TICK += Penalty - (2 + 2 * (TAHEAD_CountMiss11 >= 0)) * NA;
#endif
      if (TAHEAD_TICKH < 0) {
        TAHEAD_TICKH = 0;
      }
      if (TAHEAD_TICKH >= TAHEAD_BORNTICK) {
        TAHEAD_TICKH = TAHEAD_BORNTICK;
      }

      if (TAHEAD_TICK < 0) {
        TAHEAD_TICK = 0;
      }
      if (TAHEAD_TICK >= TAHEAD_BORNTICK) {
#ifndef TAHEAD_INTERLEAVED
        // the simulator was designed for TAHEAD_NHIST= 14
        if (TAHEAD_NHIST == 14) {
          for (int i = 1; i <= ((TAHEAD_SHARED) ? 8 : 14); i++) {
            for (int j = 0; j < TAHEAD_ASSOC * (1 << (TAHEAD_LOGG + (TAHEAD_SHARED ? (i <= 6) : 0))); j++) {
              int idxx = j % TAHEAD_getTableSize(i);
              // this is not realistic: in a real processor:    TAHEAD_gtable[1][idxx].u >>= 1;
              if (get_TAHEAD_gtable_entry(i, idxx).u > 0) {
                get_TAHEAD_gtable_entry(i, idxx).u--;
              }
            }
          }
        }

        else {
          for (int i = 1; i <= TAHEAD_NHIST; i++) {
            for (int j = 0; j < TAHEAD_ASSOC * (1 << TAHEAD_LOGG); j++) {
              int idxx = j % TAHEAD_getTableSize(i);
              // this is not realistic: in a real processor:    TAHEAD_gtable[1][idxx].u >>= 1;
              if (get_TAHEAD_gtable_entry(i, idxx).u > 0) {
                get_TAHEAD_gtable_entry(i, idxx).u--;
              }
            }
          }
        }
#else

        for (int j = 0; j < TAHEAD_ASSOC * (1 << TAHEAD_LOGG) * TAHEAD_NHIST; j++) {
          int idxx = j % TAHEAD_getTableSize(1);
          // this is not realistic: in a real processor:    TAHEAD_gtable[1][idxx].u >>= 1;
          if (get_TAHEAD_gtable_entry(1, idxx).u > 0) {
            get_TAHEAD_gtable_entry(1, idxx).u--;
          }
        }
#endif
        TAHEAD_TICK  = 0;
        TAHEAD_TICKH = 0;
      }
    }

    // update TAGE predictions

    if (TAHEAD_HitBank > 0) {
#ifdef TAHEAD_UPDATEALTONWEAKMISP
      // This protection, when prediction is low confidence
      if (TAHEAD_TAGECONF == 0) {
        if (TAHEAD_LongestMatchPred != resolveDir) {
          if (TAHEAD_AltBank != TAHEAD_HCpredBank) {
            ctrupdate(get_TAHEAD_gtable_entry(TAHEAD_AltBank, TAHEAD_GGI[TAHEAD_AltAssoc][TAHEAD_AltBank] + TAHEAD_AltAssoc).ctr,
                      resolveDir,
                      TAHEAD_CWIDTH);
          }
          if (TAHEAD_HCpredBank > 0) {
            ctrupdate(
                get_TAHEAD_gtable_entry(TAHEAD_HCpredBank, TAHEAD_GGI[TAHEAD_HCpredAssoc][TAHEAD_HCpredBank] + TAHEAD_HCpredAssoc)
                    .ctr,
                resolveDir,
                TAHEAD_CWIDTH);
          }

          else {
            baseupdate(resolveDir);
          }
        }
      }

#endif
      ctrupdate(get_TAHEAD_gtable_entry(TAHEAD_HitBank, TAHEAD_GGI[TAHEAD_HitAssoc][TAHEAD_HitBank] + TAHEAD_HitAssoc).ctr,
                resolveDir,
                TAHEAD_CWIDTH);

    } else {
      baseupdate(resolveDir);
    }
    ////////: note that here it is TAHEAD_alttaken that is used: the second hitting entry

    if (TAHEAD_LongestMatchPred != TAHEAD_alttaken) {
      if (TAHEAD_LongestMatchPred == resolveDir) {
#ifdef TAHEAD_PROTECTRECENTALLOCUSEFUL

        if (get_TAHEAD_gtable_entry(TAHEAD_HitBank, TAHEAD_GGI[TAHEAD_HitAssoc][TAHEAD_HitBank] + TAHEAD_HitAssoc).u == 0) {
          get_TAHEAD_gtable_entry(TAHEAD_HitBank, TAHEAD_GGI[TAHEAD_HitAssoc][TAHEAD_HitBank] + TAHEAD_HitAssoc).u++;
        }
        // Recent useful will survive a smart reset
#endif
        if (get_TAHEAD_gtable_entry(TAHEAD_HitBank, TAHEAD_GGI[TAHEAD_HitAssoc][TAHEAD_HitBank] + TAHEAD_HitAssoc).u
            < (1 << TAHEAD_UWIDTH) - 1) {
          get_TAHEAD_gtable_entry(TAHEAD_HitBank, TAHEAD_GGI[TAHEAD_HitAssoc][TAHEAD_HitBank] + TAHEAD_HitAssoc).u++;
        }

      } else {
        if (get_TAHEAD_gtable_entry(TAHEAD_HitBank, TAHEAD_GGI[TAHEAD_HitAssoc][TAHEAD_HitBank] + TAHEAD_HitAssoc).u > 0) {
          if (TAHEAD_predSC == resolveDir) {
            get_TAHEAD_gtable_entry(TAHEAD_HitBank, TAHEAD_GGI[TAHEAD_HitAssoc][TAHEAD_HitBank] + TAHEAD_HitAssoc).u--;
          }
        }
      }
    }

#ifdef TAHEAD_DELAY_UPDATE
    (void)PCBRANCH;
    (void)predDir;
    (void)opType;
    (void)branchTarget;
    TAHEAD_Numero++;
#else
    HistoryUpdate(PCBRANCH, opType, resolveDir, branchTarget, TAHEAD_ptghist, tahead_ch_i, TAHEAD_ch_t[0], TAHEAD_ch_t[1]);
#endif
  }

  void TrackOtherInst(uint64_t PCBRANCH, Opcode opType, bool taken, uint64_t branchTarget) {
#ifdef TAHEAD_DELAY_UPDATE
    (void)PCBRANCH;
    (void)opType;
    (void)taken;
    (void)branchTarget;
    TAHEAD_Numero++;
#else
    HistoryUpdate(PCBRANCH, opType, taken, branchTarget, TAHEAD_ptghist, tahead_ch_i, TAHEAD_ch_t[0], TAHEAD_ch_t[1]);
#endif
  }
#ifdef TAHEAD_DELAY_UPDATE
  void delayed_history(uint64_t PCBRANCH, Opcode opType, bool taken, uint64_t branchTarget) {
    HistoryUpdate(PCBRANCH, opType, taken, branchTarget, TAHEAD_ptghist, tahead_ch_i, TAHEAD_ch_t[0], TAHEAD_ch_t[1]);
  }
#endif
};

#endif
