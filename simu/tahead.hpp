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
// #define RANDINIT  // RANDINIT provide random values in all counters, might be slightly more realistic than initialization with
// weak counters

// Possible conf option if updates are delayed to end of fetch_boundary (BPred.cpp:pending)
//#define TAHEAD_DELAY_UPDATE 1


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

#define TAHEAD_MAXBR   16  // Maximum TAHEAD_MAXBR  branches in  the block; the code assumes TAHEAD_MAXBR is a power of 2
#define NBREADPERTABLE 4   // predictions read per table for a block

#define TAHEAD_AHEAD 2
// in the curent version:  only 0 or 2 are valid (0 corresponds to the conventional 1-block ahead, 2 coresponds to the 3-block
// ahead)

// FIXME: DISSABLING code
// general prameters
// Only useful when TAHEAD_AHEAD==2
#define READWIDTHAHEAD 16  // the number of entries read in each tagged table   (per way if associative),
#define TAGCHECKAHEAD  4   // the number of tag checks per entries,
//  (16,4) and (8,8) seems good design points

#define NHIST 14  // 14  different history lengths, but 7 physical tables

#define TAHEAD_UWIDTH 2
#define LOGASSOC \
  1  // associative tagged tables are probably  not worth the effort at TAHEAD_TBITS=12 : about 0.02 MPKI gain for associativity 2;
     // an extra tag bit would be  needed to get some gain with associativity 4 // but partial skewed associativity (option PSK)
     // might be interesting
#define TAHEAD_TBITS 12  // if 11 bits: benefit from associativity vanishes

#define TAHEAD_LOGG (TAHEAD_LOGT - LOGASSOC)  // size of way in a logical TAGE table
#define ASSOC       (1 << LOGASSOC)

#define HYSTSHIFT 1  // bimodal hysteresis shared among (1<< HYSTSHIFT) entries
#define BIMWIDTH  3  //  with of the counter in the bimodal predictor
// A. Seznec: I just played using 3-bit counters in the simulator, using 2-bit counters but HYSTSHIFT=0 brings similar accuracy

/////////////////////////////////////////////
// Options  for optimizations of TAGE
// #define TAHEAD_INTERLEAVED // just to show that it  is not  fully interleaving the banks is not that great and probably not worth
// the extra 14x14 shuffling/reshuffling
#ifdef TAHEAD_INTERLEAVED
#define SHARED        0
#define ADJACENTTABLE 1
int BANK1;
#endif

/////////////////////////////////////////////////
// the replacement/allocation policies described in the slide set
#define OPTTAGE
#ifdef OPTTAGE
#ifndef TAHEAD_INTERLEAVED
#define ADJACENTTABLE \
  1  // ~+0.076,  if 14 tables :7 physical tables: Logical table T(2i-1) and T(2i) are mapped on the the same physical P(i), but the
     // two predictions are adjacent and  are read with index computed with H(2i-1), the tags are respectively computed with  for
     // H(2i-1) and H(2i).
#define SHARED 1  // (T1/T9) (T2/T10)   shared the same bank T9 and T10 do not share with anybody: ~ -0.076 MPKI
#endif
#define OPTGEOHIST  // we can do better than geometric series
// Optimizations  allocation/replacement: globally; ~0.09
#define FILTERALLOCATION 1  // ~ -0.04 MPKI
#define FORCEU           1  // don't work if only one U  bit	// from times selective allocation with u = 1: ~0.015 MPKI

#if (LOGASSOC == 1)
// A. Seznec: partial skewed associativity, remmeber that I invented it in 1993 :-)
#define PSK 1
#define REPSK \
  1  // this optimization is funny, if no "useless" entry, move the entry on the other way to make room, brings a little bit of
     // accuracy
#else
#define PSK   1
#define REPSK 0
#endif

#define PROTECTRECENTALLOCUSEFUL 1  // Recently allocated entries  are protected against the smart u reset: ~ 0.007 MPKI
#define UPDATEALTONWEAKMISP \
  1  // When the Longest match is weak and wrong, one updates also the alternate prediction and HCPred : ~0.018 MPKI

#else
#define SHARED 0
#define PSK    0
#define REPSK  0
#endif
//////////////////////////////////////////////

/////////////////////////////////////////////
/// For the TAHEAD_SC component
#define TAHEAD_SC  // Enables the statistical corrector
#ifndef TAHEAD_SC
#define LMP  // systematically use LongestMatchPred, but with an optimized allocation policy.
// In practice the optimizations on TAGE brings significant gains
#endif

#define FORCEONHIGHCONF  //   if TAGE is high conf and TAHEAD_SC very low conf then use TAGE, if TAHEAD_SC: brings 0.008 - 0.016
                         //   MPKI, but a 5-to-1 mux instead a 4-to-1
// #define MORESCLOGICAHEAD // if TAHEAD_AHEAD and if TAHEAD_SC uses four times  the number of adder trees (compute 16 SCsum  per
// prediction !), ~ 1 % gain in accuracy

// Add the extra TAHEAD_SC tables
#define SCMEDIUM
#ifdef SCMEDIUM
#define SCFULL
// 4 tables for IMLI and global history variation: see slide set
#endif

#define PERCWIDTH 6  // Statistical corrector counter width: if FULL  6 bits brings 0.007
/////////////////////////////////////////////////

int NPRED = 20;  // this variable needs to be larger than TAHEAD_AHEAD to avoid core dump when TAHEAD_AHEAD prediction
// I was wanting to test large TAHEAD_AHEAD distances up to 9
uint     AHGI[10][NHIST + 1];    // indexes to the different tables are computed only once
uint     AHGTAG[10][NHIST + 1];  // tags for the different tables are computed only once
uint64_t Numero;                 // Number of the branch in the basic block
uint64_t PCBLOCK;
uint64_t PrevPCBLOCK;
uint64_t PrevNumero;

// To get the predictor storage budget on stderr  uncomment the next line
#define PRINTSIZE
#include <vector>

//////////////////////////////////
////////The statistical corrector components

// The base table  in the TAHEAD_SC component indexed with only PC + information flowing out from  TAGE
//  In order to  allow computing SCSUM in parallel with TAGE check, only LongestMatchPred and HCpred are used. 4 SCSUM are computed,
//  and a final 4-to-1 selects the correct prediction:   each extra bit of information (confidence, etc) would necessitate  doubling
//  the number of computed SCSUMs and double the width of the final MUX

// if only PC-based TAHEAD_SC these ones are useful
int8_t BiasGEN;
int8_t BiasAP[2];
int8_t BiasLM[2];
//////

int8_t BiasLMAP[4];
int8_t BiasPC[1 << TAHEAD_LOGBIAS];
int8_t BiasPCLMAP[(1 << TAHEAD_LOGBIAS)];

#define TAHEAD_LOGINB TAHEAD_LOGBIAS
int    Im = TAHEAD_LOGBIAS;
int8_t IBIAS[(1 << TAHEAD_LOGINB)];
int8_t IIBIAS[(1 << TAHEAD_LOGINB)];

// Back path history; (in practice  when a  new backward branch is  reached; 2 bits are pushed in the history
#define LOGBNB TAHEAD_LOGBIAS
int    Bm = TAHEAD_LOGBIAS;
int8_t BBIAS[(1 << LOGBNB)];
//////////////// Forward path history (taken)
#define TAHEAD_LOGFNB TAHEAD_LOGBIAS
int    Fm = TAHEAD_LOGBIAS;
int8_t FBIAS[(1 << TAHEAD_LOGFNB)];

// indices for the  TAHEAD_SC tables
#define INDBIASLMAP (LongestMatchPred + (HCpred << 1))
#define PSNUM       ((((TAHEAD_AHEAD) ? ((Numero ^ PCBLOCK) & (TAHEAD_MAXBR - 1)) : (Numero & (TAHEAD_MAXBR - 1)))) << 2)

#ifdef MORESCLOGICAHEAD
#define PCBL ((TAHEAD_AHEAD) ? (PrevPCBLOCK ^ ((GH)&3)) : (PCBLOCK))
#else
#define PCBL ((TAHEAD_AHEAD) ? (PrevPCBLOCK) : (PCBLOCK))
#endif

#define INDBIASPC     (((((PCBL ^ (PCBL >> (TAHEAD_LOGBIAS - 5))))) & ((1 << TAHEAD_LOGBIAS) - 1)) ^ PSNUM)
#define INDBIASPCLMAP (INDBIASPC) ^ ((LongestMatchPred ^ (HCpred << 1)) << (TAHEAD_LOGBIAS - 2))
// a single  physical table but  two logic tables: indices agree on all the bits except 2

#define INDBIASBHIST  (((((PCBL ^ PrevBHIST ^ (PCBL >> (TAHEAD_LOGBIAS - 4))))) & ((1 << LOGBNB) - 1)) ^ PSNUM)
#define INDBIASFHIST  (((((PCBL ^ PrevFHIST ^ (PCBL >> (TAHEAD_LOGBIAS - 3))))) & ((1 << TAHEAD_LOGFNB) - 1)) ^ PSNUM)
#define INDBIASIMLIBR (((((PCBL ^ PrevF_BrIMLI ^ (PCBL >> (TAHEAD_LOGBIAS - 6))))) & ((1 << TAHEAD_LOGINB) - 1)) ^ PSNUM)
#define INDBIASIMLITA ((((((PCBL >> 4) ^ PrevF_TaIMLI ^ (PCBL << (TAHEAD_LOGBIAS - 4))))) & ((1 << TAHEAD_LOGINB) - 1)) ^ PSNUM)

//////////////////////IMLI RELATED and backward/Forward history////////////////////////////////////
long long TaIMLI;    // use to monitor the iteration number (based on target locality for backward branches)
long long BrIMLI;    // use to monitor the iteration number (a second version based on backward branch locality))
long long F_TaIMLI;  // use to monitor the iteration number,BHIST if TaIMLI = 0
long long F_BrIMLI;  // use to monitor the iteration number (a second version), FHIST if BrIMLI = 0
long long BHIST;
long long FHIST;

// Same thing but a cycle TAHEAD_AHEAD
long long PrevF_TaIMLI;  // use to monitor the iteration number, BHIST if TaIMLI = 0
long long PrevF_BrIMLI;  // use to monitor the iteration number (a second version), FHIST if BrIMLI = 0
long long PrevBHIST;
long long PrevFHIST;

// Needs for computing the "histories" for IMLI and backward/forward histories
uint64_t LastBack;
uint64_t LastBackPC;
uint64_t BBHIST;

// update threshold for the statistical corrector
#define WIDTHRES 8
int updatethreshold;

int SUMSC;
int SUMFULL;

bool predTSC;
bool predSC;
bool pred_inter;

////  FOR TAGE //////

#define HISTBUFFERLENGTH 4096  // we use a 4K entries history buffer to store the branch history

#define BORNTICK 4096
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
    comp = (comp << 1) ^ h[PT & (HISTBUFFERLENGTH - 1)];

    comp ^= h[(PT + OLENGTH) & (HISTBUFFERLENGTH - 1)] << OUTPOINT;
    comp ^= (comp >> CLENGTH);
    comp = (comp) & ((1 << CLENGTH) - 1);
  }
};

class bentry  // TAGE bimodal table entry
{
public:
  int8_t hyst;
  int8_t pred;
  bentry() {
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

bool alttaken;  // alternate   TAGE prediction if the longest match was not hitting: needed for updating the u bit
bool HCpred;    // longest not low confident match or base prediction if no confident match

bool   tage_pred;  // TAGE prediction
bool   LongestMatchPred;
int    HitBank;     // longest matching bank
int    AltBank;     // alternate matching bank
int    HCpredBank;  // longest non weak  matching bank
int    HitAssoc;
int    AltAssoc;
int    HCpredAssoc;
int    Seed;  // for the pseudo-random number generator
int8_t BIM;   // the bimodal prediction

int8_t CountMiss11  = -64;  // more or less than 11% of misspredictions
int8_t CountLowConf = 0;

int8_t COUNT50[NHIST + 1];     // more or less than 50%  misprediction on weak LongestMatchPred
int8_t COUNT16_31[NHIST + 1];  // more or less than 16/31th  misprediction on weak LongestMatchPred
int    TAGECONF;               // TAGE confidence  from 0 (weak counter) to 3 (saturated)

#define TAHEAD_PHISTWIDTH 27  // width of the path history used in TAGE
#define TAHEAD_CWIDTH     3   // predictor counter width on the TAGE tagged tables

// the counter(s) to chose between longest match and alternate prediction on TAGE when weak counters: only plain TAGE
#define TAHEAD_ALTWIDTH 5
int8_t use_alt_on_na;
int    TICK, TICKH;  // for the reset of the u counter

uint8_t ghist[HISTBUFFERLENGTH];
int     ptghist;
// for managing global path history

long long             phist;                   // path history
int                   GH;                      //  another form of path history
TAHEAD_folded_history tahead_ch_i[NHIST + 1];  // utility for computing TAGE indices
TAHEAD_folded_history ch_t[2][NHIST + 1];      // utility for computing TAGE tags

// For the TAGE predictor
bentry        *btable;             // bimodal TAGE table
TAHEAD_gentry *gtable[NHIST + 1];  // tagged TAGE tables
int            TAHEAD_m[NHIST + 1];
uint           GI[NHIST + 1];          // indexes to the different tables are computed only once
uint           GGI[ASSOC][NHIST + 1];  // indexes to the different tables are computed only once
uint           GTAG[NHIST + 1];        // tags for the different tables are computed only once
int            BI;                     // index of the bimodal table
bool           pred_taken;             // prediction

int incval(int8_t ctr) {
  return (2 * ctr + 1);
  // to center the sum
  //  probably not worth, but don't understand why
}

int predictorsize() {
  int STORAGESIZE = 0;
  int inter       = 0;

  STORAGESIZE += NHIST * (1 << TAHEAD_LOGG) * (TAHEAD_CWIDTH + TAHEAD_UWIDTH + TAHEAD_TBITS) * ASSOC;
#ifndef TAHEAD_SC
  STORAGESIZE += TAHEAD_ALTWIDTH;
  // the use_alt counter
#endif
  STORAGESIZE += (1 << TAHEAD_LOGB) + (BIMWIDTH - 1) * (1 << (TAHEAD_LOGB - HYSTSHIFT));
  STORAGESIZE += TAHEAD_m[NHIST];      // the history bits
  STORAGESIZE += TAHEAD_PHISTWIDTH;    // phist
  STORAGESIZE += 12;                   // the TICK counter
  STORAGESIZE += 12;                   // the TICKH counter
  STORAGESIZE += 2 * 7 * (NHIST / 4);  // counters COUNT50 COUNT16_31
  STORAGESIZE += 8;                    // CountMiss11
  STORAGESIZE += 36;                   // for the random number generator
  fprintf(stderr, " (TAGE %d) ", STORAGESIZE);
#ifdef TAHEAD_SC

  inter += WIDTHRES;
  inter += (PERCWIDTH)*2 * (1 << TAHEAD_LOGBIAS);  // BiasPC and BiasPCLMAP,
  inter += (PERCWIDTH)*2;                          // BiasLMAP

#ifdef SCMEDIUM
#ifdef SCFULL

  inter += (1 << TAHEAD_LOGFNB) * PERCWIDTH;
  inter += TAHEAD_LOGFNB;
  inter += (1 << LOGBNB) * PERCWIDTH;
  inter += LOGBNB;
  inter += (1 << TAHEAD_LOGINB) * PERCWIDTH;  // two forms
  inter += TAHEAD_LOGBIAS;
  inter += 10;  // LastBackPC
#endif
  inter += (1 << TAHEAD_LOGINB) * PERCWIDTH;  // two forms
  inter += TAHEAD_LOGBIAS;
  inter += 10;  // LastBack
#endif

  STORAGESIZE += inter;

  fprintf(stderr, " (TAHEAD_SC %d) ", inter);
#endif
#ifdef PRINTSIZE

  fprintf(stderr, " (TOTAL %d, %d Kbits)\n  ", STORAGESIZE, STORAGESIZE / 1024);
  fprintf(stdout, " (TOTAL %d %d Kbits)\n  ", STORAGESIZE, STORAGESIZE / 1024);
#endif

  return (STORAGESIZE);
}

class Tahead {
public:
  Tahead(void) {
    reinit();
#ifdef PRINTSIZE
    predictorsize();
#endif
  }

#define NNHIST 18
  int mm[NNHIST + 1];

  void reinit() {
    if ((TAHEAD_AHEAD != 0) && (TAHEAD_AHEAD != 2)) {
      printf("Sorry the simulator does not support this TAHEAD_AHEAD distance\n");
      exit(1);
    }
    if ((LOGASSOC != 1) || (PSK == 0)) {
#if (REPSK == 1)

      printf("Sorry REPSK only with associativity 2 and PSK activated\n");
      exit(1);

#endif
    }

#ifdef OPTGEOHIST
    mm[1] = TAHEAD_MINHIST;

    for (int i = 2; i <= NNHIST; i++) {
      mm[i] = (int)(((double)TAHEAD_MINHIST
                     * pow((double)(TAHEAD_MAXHIST) / (double)TAHEAD_MINHIST, (double)(i - 1) / (double)((NNHIST - 1))))
                    + 0.5);
    }
    for (int i = 2; i <= NNHIST; i++) {
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
    PT = NHIST;

    for (int i = NNHIST; i > 14; i -= 2) {
      TAHEAD_m[PT] = mm[i];
      PT--;
    }

#else
    TAHEAD_m[1] = TAHEAD_MINHIST;

    for (int i = 2; i <= NHIST; i++) {
      TAHEAD_m[i] = (int)(((double)TAHEAD_MINHIST
                           * pow((double)(TAHEAD_MAXHIST) / (double)TAHEAD_MINHIST, (double)(i - 1) / (double)((NHIST - 1))))
                          + 0.5);
    }
    for (int i = 3; i <= NHIST; i++) {
      if (TAHEAD_m[i] <= TAHEAD_m[i - 1]) {
        TAHEAD_m[i] = TAHEAD_m[i - 1] + 1;
      }
    }
#endif
    if ((TAHEAD_AHEAD != 0) & (TAHEAD_AHEAD != 2)) {
      exit(1);  // prediction is considered to be done in 1 cycle or 3 cycles
    }
    for (int i = 1; i <= NHIST; i++) {
      TAHEAD_m[i] -= TAHEAD_AHEAD;
    }

#ifdef ADJACENTTABLE
    if (LOGASSOC == 0) {
      //  if there is some associativity: no need for this
      for (int i = 2; i <= NHIST; i += 2)

      {
        TAHEAD_m[i] = TAHEAD_m[i - 1] + ((TAHEAD_m[i] - TAHEAD_m[i - 1]) / 2);

        if (TAHEAD_m[i] == TAHEAD_m[i - 1]) {
          TAHEAD_m[i]++;
        }
      }
    }

#endif
    for (int i = 1; i <= NHIST; i++) {
      TAHEAD_m[i] <<= 2;
    }
    // 4 bits per block

    for (int i = 1; i <= NHIST; i++) {
      printf("%d ", TAHEAD_m[i]);
    }
    printf("\n");
#ifndef TAHEAD_INTERLEAVED
    if (SHARED) {
      /* tailored for 14 tables */
      for (int i = 1; i <= 8; i++) {
        gtable[i] = new TAHEAD_gentry[(1 << (TAHEAD_LOGG + (i <= 6))) * ASSOC];
      }
      for (int i = 9; i <= 14; i++) {
        gtable[i] = gtable[i - 8];
      }
    }

    else {
      for (int i = 1; i <= NHIST; i++) {
        gtable[i] = new TAHEAD_gentry[(1 << (TAHEAD_LOGG)) * ASSOC];
      }
    }
#else
    gtable[1] = new TAHEAD_gentry[(1 << (TAHEAD_LOGG)) * ASSOC * NHIST];

    for (int i = 2; i <= NHIST; i++) {
      gtable[i] = gtable[1];
    }

#endif

    btable = new bentry[1 << TAHEAD_LOGB];
    for (int i = 1; i <= NHIST; i++) {
      tahead_ch_i[i].init(TAHEAD_m[i], 25 + (2 * ((i - 1) / 2) % 4), i - 1);
      ch_t[0][i].init(tahead_ch_i[i].OLENGTH, 13, i);
      ch_t[1][i].init(tahead_ch_i[i].OLENGTH, 11, i + 2);
    }

    Seed = 0;

    TICK  = 0;
    phist = 0;
    Seed  = 0;

    for (int i = 0; i < HISTBUFFERLENGTH; i++) {
      ghist[0] = 0;
    }
    ptghist = 0;

    updatethreshold = 23;

#ifdef SCMEDIUM

    for (int j = 0; j < ((1 << LOGBNB) - 1); j++) {
      if (!(j & 1)) {
        BBIAS[j] = -1;
      }
    }
    for (int j = 0; j < ((1 << TAHEAD_LOGFNB) - 1); j++) {
      if (!(j & 1)) {
        FBIAS[j] = -1;
      }
    }
    for (int j = 0; j < ((1 << TAHEAD_LOGINB) - 1); j++) {
      if (!(j & 1)) {
        IBIAS[j] = -1;
      }
    }
    for (int j = 0; j < ((1 << TAHEAD_LOGINB) - 1); j++) {
      if (!(j & 1)) {
        IIBIAS[j] = -1;
      }
    }

#endif

    for (int j = 0; j < (1 << TAHEAD_LOGBIAS); j++) {
      switch (j & 3) {
        case 0: BiasPCLMAP[j] = -8; break;
        case 1: BiasPCLMAP[j] = 7; break;
        case 2: BiasPCLMAP[j] = -32; break;
        case 3: BiasPCLMAP[j] = 31; break;
      }
    }

    TICK = 0;

    ptghist = 0;
    phist   = 0;
#ifdef RANDINIT
    if (NHIST == 14) {
      for (int i = 1; i <= ((SHARED) ? 8 : 14); i++) {
        for (int j = 0; j < ASSOC * (1 << (TAHEAD_LOGG + (SHARED ? (i <= 6) : 0))); j++) {
          gtable[i][j].u = random() & ((1 << TAHEAD_UWIDTH) - 1);

          gtable[i][j].ctr = (random() & 7) - 4;
        }
      }
    }

    else {
      for (int i = 1; i <= NHIST; i++) {
        for (int j = 0; j < ASSOC * (1 << TAHEAD_LOGG); j++) {
          gtable[i][j].u   = random() & ((1 << TAHEAD_UWIDTH) - 1);
          gtable[i][j].ctr = (random() & 7) - 4;
        }
      }
    }

    TICK  = BORNTICK / 2;
    TICKH = BORNTICK / 2;
    for (int i = 0; i < (1 << TAHEAD_LOGB); i++) {
      btable[i].pred = random() & 1;
      btable[i].hyst = random() & 3;
    }
    updatethreshold = 23;
#ifdef SCMEDIUM
    for (int j = 0; j < ((1 << LOGBNB) - 1); j++) {
      BBIAS[j] = -(1 << (PERCWIDTH - 1)) + (random() & ((1 << PERCWIDTH) - 1));
    }
    for (int j = 0; j < ((1 << TAHEAD_LOGFNB) - 1); j++) {
      FBIAS[j] = -(1 << (PERCWIDTH - 1)) + (random() & ((1 << PERCWIDTH) - 1));
    }
    for (int j = 0; j < ((1 << TAHEAD_LOGINB) - 1); j++) {
      IBIAS[j] = -(1 << (PERCWIDTH - 1)) + (random() & ((1 << PERCWIDTH) - 1));
    }
    for (int j = 0; j < ((1 << TAHEAD_LOGINB) - 1); j++) {
      IIBIAS[j] = -(1 << (PERCWIDTH - 1)) + (random() & ((1 << PERCWIDTH) - 1));
    }

#endif
    for (int j = 0; j < (1 << TAHEAD_LOGBIAS); j++) {
      BiasPCLMAP[j] = -(1 << (PERCWIDTH - 1)) + (random() & ((1 << PERCWIDTH) - 1));
      BiasPC[j]     = -(1 << (PERCWIDTH - 1)) + (random() & ((1 << PERCWIDTH) - 1));
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

  // gindex computes a full hash of PC, ghist and phist
  uint gindex(unsigned int PC, int bank, long long hist, TAHEAD_folded_history *ptahead_ch_i) {
    uint index;
    int  logg  = TAHEAD_LOGG + /* SHARED+*/ (SHARED & (bank <= 1));
    uint M     = (TAHEAD_m[bank] > TAHEAD_PHISTWIDTH) ? TAHEAD_PHISTWIDTH : TAHEAD_m[bank];
    index      = PC ^ (PC >> (abs(logg - bank) + 1)) ^ ptahead_ch_i[bank].comp ^ F(hist, M, bank);
    uint32_t X = (index ^ (index >> logg) ^ (index >> 2 * logg)) & ((1 << logg) - 1);
#ifdef TAHEAD_INTERLEAVED
    if (bank == 1) {
      BANK1 = index % NHIST;
    }
#endif

    return (X);
  }

  //  tag computation
  uint16_t gtag(unsigned int PC, int bank, TAHEAD_folded_history *ch0, TAHEAD_folded_history *ch1) {
    int tag = PC ^ (PC >> 2);
    int M   = (TAHEAD_m[bank] > TAHEAD_PHISTWIDTH) ? TAHEAD_PHISTWIDTH : TAHEAD_m[bank];
    tag     = (tag >> 1) ^ ((tag & 1) << 10) ^ F(phist, M, bank);
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
    BIM      = (btable[BI].pred) ? (btable[BI >> HYSTSHIFT].hyst) : -1 - (btable[BI >> HYSTSHIFT].hyst);
    TAGECONF = 3 * (btable[BI >> HYSTSHIFT].hyst != 0);

    return (btable[BI].pred != 0);
  }

  void baseupdate(bool Taken) {
    int8_t inter = BIM;
    ctrupdate(inter, Taken, BIMWIDTH);
    btable[BI].pred              = (inter >= 0);
    btable[BI >> HYSTSHIFT].hyst = (inter >= 0) ? inter : -inter - 1;
  };
  uint32_t MYRANDOM() {
    // This pseudo-random function: just to be sure that the simulator is deterministic
    //  results are within +- 0.002 MPKI in average with some larger difference on individual benchmarks
    Seed++;
    Seed += phist;
    Seed = (Seed >> 21) + (Seed << 11);
    Seed += ptghist;
    Seed = (Seed >> 10) + (Seed << 22);
    Seed += GTAG[4];
    return (Seed);
  };

  //  TAGE PREDICTION: same code at fetch or retire time but the index and tags must recomputed
  void Tagepred(uint64_t PC) {
    HitBank    = 0;
    AltBank    = 0;
    HCpredBank = 0;
    if (Numero == 0) {
      for (int i = 1; i <= NHIST; i++) {
        AHGI[NPRED % 10][i]   = gindex(PC, i, phist, tahead_ch_i);
        AHGTAG[NPRED % 10][i] = gtag(PC, i, ch_t[0], ch_t[1]);
      }
      if (SHARED) {
        int X = AHGI[NPRED % 10][1] & 1;
        for (int i = 2; i <= 6; i++) {
          AHGI[NPRED % 10][i] <<= 1;
          AHGI[NPRED % 10][i] ^= X;
        }
        for (int i = 9; i <= 14; i++) {
          AHGI[NPRED % 10][i] <<= 1;
          AHGI[NPRED % 10][i] ^= X ^ 1;
        }
      }
#ifdef TAHEAD_INTERLEAVED
#ifndef ADJACENTTABLE
      for (int i = 1; i <= NHIST; i++) {
        AHGI[NPRED % 10][i] += ((BANK1 + i) % NHIST) * (1 << (TAHEAD_LOGG));
      }
#else
      for (int i = 2; i <= NHIST; i += 2) {
        AHGI[NPRED % 10][i] = AHGI[NPRED % 10][i - 1];
      }
      for (int i = 1; i <= NHIST; i++) {
        AHGI[NPRED % 10][i] += ((BANK1 + ((i - 1) / 2)) % (NHIST / 2)) * (1 << (TAHEAD_LOGG + 1)) + ((i & 1) << TAHEAD_LOGG);
      }

#endif
#endif
    }
    int AHEADTAG = (TAHEAD_AHEAD > 0) ? TAHEAD_AHEAD - 1 : TAHEAD_AHEAD;
    // assumes that the tag is used one cycle later than the index if TAHEAD_AHEAD pipelining is used.

    BI = (PCBLOCK ^ ((Numero & (NBREADPERTABLE - 1)) << (TAHEAD_LOGB - 2))) & ((1 << TAHEAD_LOGB) - 1);

    // For TAHEAD_AHEAD, one considers that the bimodal prediction is  obtained during the last cycle
    for (int i = 1; i <= NHIST; i++) {
#if (TAHEAD_AHEAD != 0)
      {
        GI[i] = AHGI[(NPRED - TAHEAD_AHEAD) % 10][i]
                ^ (((GH ^ Numero ^ BI ^ (PCBLOCK >> 3)) & (READWIDTHAHEAD - 1)) << (TAHEAD_LOGG - LOGASSOC - 4));
        // some bits are hashed on  values that are unknown at prediction read time: assumes READWITHTAHEAD reads at a time

        GI[i] *= ASSOC;
        GTAG[i] = AHGTAG[(NPRED - AHEADTAG) % 10][i]

                  ^ (((GH ^ (GH >> 1)) & (TAGCHECKAHEAD - 1)));

        ;

        // TAGCHECKAHEAD reads per read entry
      }

#else
      {
        GI[i] = AHGI[(NPRED - TAHEAD_AHEAD) % 10][i] ^ ((Numero & (NBREADPERTABLE - 1)) << (TAHEAD_LOGG - LOGASSOC - 2));
        GI[i] *= ASSOC;
        GTAG[i] = AHGTAG[(NPRED - AHEADTAG) % 10][i] ^ (Numero);
      }
#endif
    }
#ifndef TAHEAD_INTERLEAVED
#ifdef ADJACENTTABLE
    for (int i = 2; i <= NHIST; i += 2) {
      GI[i] = GI[i - 1];
    }

#endif
#endif
    for (int i = 1; i <= NHIST; i++) {
      for (int j = 0; j < ASSOC; j++) {
        GGI[j][i] = GI[i];
      }
      if (PSK == 1) {
        if (TAHEAD_AHEAD == 0) {
          for (int j = 1; j < ASSOC; j++) {
            GGI[j][i] ^= ((GTAG[i] >> (3 + 2 * j)) & 0x3) << (TAHEAD_LOGG - 3);
          }
        } else {
          for (int j = 1; j < ASSOC; j++) {
            GGI[j][i] ^= ((GTAG[i] >> (3 + 2 * j)) & (READWIDTHAHEAD - 1)) << (TAHEAD_LOGG - 5);
          }
        }
      }

      // works for TAHEAD_AHEAD also if READWIDTHAHEAD <= 16
    }

    alttaken         = getbim();
    HCpred           = alttaken;
    tage_pred        = alttaken;
    LongestMatchPred = alttaken;

    // Look for the bank with longest matching history
    for (int i = NHIST; i > 0; i--) {
      for (int j = 0; j < ASSOC; j++) {
        if (gtable[i][GGI[j][i] + j].tag == GTAG[i]) {
          HitBank  = i;
          HitAssoc = j;

          LongestMatchPred = (gtable[HitBank][GGI[HitAssoc][HitBank] + HitAssoc].ctr >= 0);
          TAGECONF         = (abs(2 * gtable[HitBank][GGI[HitAssoc][HitBank] + HitAssoc].ctr + 1)) >> 1;

          break;
        }
      }
      if (HitBank > 0) {
        break;
      }
    }
    // should be noted that when LongestMatchPred is not low conf then alttaken is the 2nd not-low conf:  not a critical path,
    // needed only on update.
    for (int i = HitBank - 1; i > 0; i--) {
      for (int j = 0; j < ASSOC; j++) {
        if (gtable[i][GGI[j][i] + j].tag == GTAG[i]) {
          // if (abs (2 * gtable[i][GGI[j][i] + j].ctr + 1) != 1)
          // slightly better to pick alternate prediction as not low confidence
          {
            AltAssoc = j;
            AltBank  = i;
            break;
          }
        }
      }
      if (AltBank > 0) {
        break;
      }
    }
    if (HitBank > 0) {
      if (abs(2 * gtable[HitBank][GGI[HitAssoc][HitBank] + HitAssoc].ctr + 1) == 1) {
        for (int i = HitBank - 1; i > 0; i--) {
          for (int j = 0; j < ASSOC; j++) {
            if (gtable[i][GGI[j][i] + j].tag == GTAG[i]) {
              if (abs(2 * gtable[i][GGI[j][i] + j].ctr + 1) != 1)
              // slightly better to pick alternate prediction as not low confidence
              {
                HCpredBank = i;

                HCpredAssoc = j;
                HCpred      = (gtable[i][GGI[j][i] + j].ctr >= 0);

                break;
              }
            }
          }
          if (HCpredBank > 0) {
            break;
          }
        }
      }

      else {
        HCpredBank  = HitBank;
        HCpredAssoc = HitAssoc;
        HCpred      = LongestMatchPred;
      }
    }

    // computes the prediction and the alternate prediction

    if (HitBank > 0) {
      if (AltBank > 0) {
        alttaken = (gtable[AltBank][GGI[AltAssoc][AltBank] + AltAssoc].ctr >= 0);
      }

#ifndef TAHEAD_SC
      // if the entry is recognized as a newly allocated entry and
      // USE_ALT_ON_NA is positive  use the alternate prediction
      bool Huse_alt_on_na = (use_alt_on_na >= 0);

      if ((!Huse_alt_on_na) || (abs(2 * gtable[HitBank][GGI[HitAssoc][HitBank] + HitAssoc].ctr + 1) > 1)) {
        tage_pred = LongestMatchPred;
      } else {
        tage_pred = HCpred;
      }

#else
      tage_pred = LongestMatchPred;
#endif
    }
  }

  // compute the prediction

  void fetchBoundaryEnd() {
#ifdef TAHEAD_DELAY_UPDATE
    // Numero = 0;
#endif
  }

  bool getPrediction(uint64_t PCBRANCH) {
    (void)PCBRANCH;

    uint64_t PC = PCBLOCK ^ (Numero << 5);

    // computes the TAGE table addresses and the partial tags
    Tagepred(PC);
    pred_taken = tage_pred;
    predSC     = pred_taken;
    predTSC    = pred_taken;

    // printf("pc:%lx Num:%lx ptaken:%d\n", PC, Numero, pred_taken);
#ifdef TAHEAD_DELAY_UPDATE
    Numero++;
#endif

#ifndef TAHEAD_SC
#ifdef LMP
    return (LongestMatchPred);
#endif
    return pred_taken;
#endif
    if (TAHEAD_AHEAD) {
      PC = PrevPCBLOCK ^ (Numero << 5) ^ (PrevNumero << 5) ^ ((BI & 3) << 5);
    }

    // Let us  compute the TAHEAD_SC prediction
    SUMSC = 0;
////// These extra counters seem to bring a marginal  gain of 0.006 MPKI  when only pure TAHEAD_SC, not useful when other info
#ifndef SCMEDIUM
    SUMSC += incval(BiasGEN);
    SUMSC += incval(BiasLM[LongestMatchPred]);
    SUMSC += incval(BiasAP[HCpred]);
#endif
    //////

    SUMSC += incval(BiasLMAP[INDBIASLMAP]);
    // x 2: a little bit better
    SUMSC += 2 * incval(BiasPC[INDBIASPC]);
    SUMSC += incval(BiasPCLMAP[INDBIASPCLMAP]);

    predTSC = (SUMSC >= 0);
    // when predTSC is correct we do not allocate any new entry
#ifdef SCMEDIUM
    SUMFULL = 0;
    SUMFULL += incval(IIBIAS[INDBIASIMLIBR]);
#ifdef SCFULL
    SUMFULL += incval(FBIAS[INDBIASFHIST]);
    SUMFULL += incval(BBIAS[INDBIASBHIST]);
    SUMFULL += incval(IBIAS[INDBIASIMLITA]);

#endif

    // x 2: a little bit better

    SUMSC += 2 * SUMFULL;
#endif
    bool SCPRED = (SUMSC >= 0);

#ifdef FORCEONHIGHCONF
    pred_taken = (TAGECONF != 3) || (abs(SUMSC) >= updatethreshold / 2) ? SCPRED : LongestMatchPred;
#else
    pred_taken = (SUMSC >= 0);
#endif

    predSC = (SUMSC >= 0);

    return pred_taken;
  }

  void HistoryUpdate(uint64_t PCBRANCH, Opcode opType, bool taken, uint64_t branchTarget, int &Y, TAHEAD_folded_history *H,
                     TAHEAD_folded_history *G, TAHEAD_folded_history *J) {
    int brtype;

    if ((Numero == TAHEAD_MAXBR - 1) || (taken)) {
      GH = (GH << 2) ^ PCBRANCH;

      uint64_t PC = PCBLOCK ^ (Numero << 5);
      NPRED++;
      GH <<= 2;
      uint64_t Successor = (taken) ? branchTarget ^ (branchTarget >> 4) : (PCBRANCH + 1) ^ ((PCBRANCH + 1) >> 4);
      GH ^= ((Numero) ^ Successor);
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
#ifdef SCMEDIUM
      PrevF_TaIMLI = F_TaIMLI;
      PrevF_BrIMLI = F_BrIMLI;
      PrevBHIST    = BHIST;
      PrevFHIST    = FHIST;
      if (taken) {
        if (branchTarget > PCBRANCH) {
          FHIST = (FHIST << 3) ^ (branchTarget >> 2) ^ (PCBRANCH >> 1);
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
            if (((branchTarget & 65535) >> 6) == LastBack) {
              if (TaIMLI < ((1 << TAHEAD_LOGBIAS) - 1)) {
                TaIMLI++;
              }
            } else {
              BBHIST = (BBHIST << 1) ^ LastBack;
              TaIMLI = 0;
            }
            if (((PCBRANCH & 65535) >> 6) == LastBackPC)

            {
              if (BrIMLI < ((1 << TAHEAD_LOGBIAS) - 1)) {
                BrIMLI++;
              }
            } else {
              BBHIST = (BBHIST << 1) ^ LastBackPC;
              BrIMLI = 0;
            }
            LastBack   = (branchTarget & 65535) >> 6;
            LastBackPC = (PCBRANCH & 65535) >> 6;
          }
        }
      }
#endif
      if (TAHEAD_AHEAD) {
        // to hash with Numero
        PrevNumero = (Numero & 1) << 1;
        PrevNumero ^= (Numero >> 1);
      }

      Numero <<= 1;
      Numero += taken;

      int T = ((PC ^ (PC >> 2))) ^ Numero ^ (branchTarget >> 3);

      int PATH = PC ^ (PC >> 2) ^ (PC >> 4) ^ (branchTarget) ^ (Numero << 3);
      phist    = (phist << 4) ^ PATH;
      phist    = (phist & ((1 << 27) - 1));

      for (int t = 0; t < 4; t++) {
        int DIR = (T & 1);
        T >>= 1;
        PATH >>= 1;
        Y--;
        ghist[Y & (HISTBUFFERLENGTH - 1)] = DIR;
        for (int i = 1; i <= NHIST; i++) {
          H[i].update(ghist, Y);
          G[i].update(ghist, Y);
          J[i].update(ghist, Y);
        }
      }

      Numero = 0;

      PrevPCBLOCK = PCBLOCK;
      PCBLOCK     = (taken) ? branchTarget : PCBRANCH + 1;
      PCBLOCK     = PCBLOCK ^ (PCBLOCK >> 4);

    } else {
      Numero++;
    }
    BHIST    = (BrIMLI == 0) ? BBHIST : ((BBHIST & 15) + (BrIMLI << 6)) ^ (BrIMLI >> 4);
    F_TaIMLI = (TaIMLI == 0) || (BrIMLI == TaIMLI) ? (GH) : TaIMLI;
    F_BrIMLI = (BrIMLI == 0) ? (phist) : BrIMLI;

    if (TAHEAD_AHEAD == 0) {
      PrevF_TaIMLI = F_TaIMLI;
      PrevF_BrIMLI = F_BrIMLI;
      PrevBHIST    = BHIST;
      PrevFHIST    = FHIST;
    }
  }

  // END UPDATE  HISTORIES

  // Tahead UPDATE

  void updatePredictor(uint64_t PCBRANCH, Opcode opType, bool resolveDir, bool predDir, uint64_t branchTarget) {
    // uint64_t PC = PCBLOCK ^ (Numero << 5);
    //
    // if (TAHEAD_AHEAD) {
    //   PC = PrevPCBLOCK ^ (Numero << 5) ^ (PrevNumero << 5) ^ ((BI & 3) << 5);
    // }
    //bool DONE = false;
#ifndef TAHEAD_DELAY_UPDATE 
(void)predDir;
#endif
#ifdef TAHEAD_SC
    bool SCPRED = (SUMSC >= 0);
    if ((SCPRED != resolveDir) || ((abs(SUMSC) < updatethreshold)))

    {
      if (SCPRED != resolveDir) {
        if (updatethreshold < (1 << (WIDTHRES)) - 1) {
          updatethreshold += 1;
        }
      } else {
        if (updatethreshold > 0) {
          updatethreshold -= 1;
        }
      }

      ctrupdate(BiasGEN, resolveDir, PERCWIDTH);
      ctrupdate(BiasAP[HCpred], resolveDir, PERCWIDTH);
      ctrupdate(BiasLM[LongestMatchPred], resolveDir, PERCWIDTH);
      ctrupdate(BiasLMAP[INDBIASLMAP], resolveDir, PERCWIDTH);

      ctrupdate(BiasPC[INDBIASPC], resolveDir, PERCWIDTH);
      ctrupdate(BiasPCLMAP[INDBIASPCLMAP], resolveDir, PERCWIDTH);

#ifdef SCMEDIUM

      ctrupdate(IBIAS[INDBIASIMLITA], resolveDir, PERCWIDTH);
      ctrupdate(IIBIAS[INDBIASIMLIBR], resolveDir, PERCWIDTH);
      ctrupdate(BBIAS[INDBIASBHIST], resolveDir, PERCWIDTH);
      ctrupdate(FBIAS[INDBIASFHIST], resolveDir, PERCWIDTH);

#endif
    }

#endif

    // TAGE UPDATE
    bool ALLOC = (HitBank < NHIST);
    ALLOC &= (LongestMatchPred != resolveDir);
    ALLOC &= (predTSC != resolveDir);
    if (HitBank > 0) {
      if ((TAGECONF == 0) || ((MYRANDOM() & 3) == 0)) {
        ctrupdate(CountLowConf, (TAGECONF == 0), 7);
      }
    }

    //////////////////////////////////////////////////

    if (HitBank > 0) {
      bool PseudoNewAlloc = (abs(2 * gtable[HitBank][GGI[HitAssoc][HitBank] + HitAssoc].ctr + 1) <= 1);
      // an entry is considered as newly allocated if its prediction counter is weak

      if (PseudoNewAlloc) {
#ifndef TAHEAD_SC
        if (LongestMatchPred == resolveDir) {
          ALLOC = false;
        }

        if (LongestMatchPred != HCpred) {
          ctrupdate(use_alt_on_na, (HCpred == resolveDir), TAHEAD_ALTWIDTH);
          // pure TAGE only
        }
#endif
      }
    }

/////////////////////////
#ifdef FILTERALLOCATION
    // filter allocations: all of this is done at update, not on the critical path
    //  try to evaluate if the misprediction rate is above 1/9th

    if ((tage_pred != resolveDir) || ((MYRANDOM() & 31) < 4)) {
      ctrupdate(CountMiss11, (tage_pred != resolveDir), 8);
    }

    if (HitBank > 0) {
      bool PseudoNewAlloc = (TAGECONF == 0);

      if (PseudoNewAlloc) {
        // Here we count correct/wrong weak counters to guide allocation
        for (int i = HitBank / 4; i <= NHIST / 4; i++) {
          ctrupdate(COUNT50[i], (resolveDir == LongestMatchPred), 7);  // more or less than 50 % good predictions on weak counters
          if ((LongestMatchPred != resolveDir) || ((MYRANDOM() & 31) > 1)) {
            ctrupdate(COUNT16_31[i],
                      (resolveDir == LongestMatchPred),
                      7);  // more or less than 16/31  good predictions on weak counters
          }
        }
      }
    }
    //  when allocating a new entry is unlikely to result in a good prediction, rarely allocate

    if ((COUNT50[(HitBank + 1) / 4] < 0)) {
      ALLOC &= ((MYRANDOM() & ((1 << (3)) - 1)) == 0);
    } else
      // the future allocated entry is not that likely to be correct
      if ((COUNT16_31[(HitBank + 1) / 4] < 0)) {
        ALLOC &= ((MYRANDOM() & ((1 << 1) - 1)) == 0);
      }
// The benefit is essentially to decrease the number of allocations
#endif

    if (ALLOC) {
      int MaxNALLOC = (CountMiss11 < 0) + 8 * (CountLowConf >= 0);
      // this CountLowConf is not very useful :-)

      int NA      = 0;
      int DEP     = HitBank + 1;
      int Penalty = 0;
      DEP += ((MYRANDOM() & 1) == 0);
      DEP += ((MYRANDOM() & 3) == 0);
      if (DEP == HitBank) {
        DEP = HitBank + 1;
      }

      bool First = true;
      bool Test  = false;

      for (int i = DEP; i <= NHIST; i++) {
#ifdef FILTERALLOCATION
        // works because the physical tables are shared
        if (SHARED) {
          if ((i > 8) & (!Test)) {
            Test = true;

            if ((CountMiss11 >= 0)) {
              if ((MYRANDOM() & 7) > 0) {
                break;
              }
            }
          }
        }
#endif
        bool done = false;
        uint j    = (MYRANDOM() % ASSOC);
        {
          bool REP[2]  = {false};
          int  IREP[2] = {0};
          bool MOVE[2] = {false};
          for (int J = 0; J < ASSOC; J++) {
            j++;
            j = j % ASSOC;

            if (gtable[i][GGI[j][i] + j].u == 0) {
              REP[j]  = true;
              IREP[j] = GGI[j][i] + j;

            }

            else if (REPSK == 1) {
              if (TAHEAD_AHEAD == 0) {
                IREP[j] = GGI[j][i] ^ ((((gtable[i][GGI[j][i] + j].tag >> 5) & 3) << (TAHEAD_LOGG - 3)) + (j ^ 1));
              } else {
                IREP[j]
                    = GGI[j][i] ^ ((((gtable[i][GGI[j][i] + j].tag >> 5) & (READWIDTHAHEAD - 1)) << (TAHEAD_LOGG - 5)) + (j ^ 1));
              }

              REP[j] = (gtable[i][IREP[j]].u == 0);

              MOVE[j] = true;
            }

            if (REP[j]) {
              if ((((TAHEAD_UWIDTH == 1) && ((((MYRANDOM() & ((1 << (abs(2 * gtable[i][GGI[j][i] + j].ctr + 1) >> 1)) - 1)) == 0))))
                   || (TICKH >= BORNTICK / 2))
                  || (TAHEAD_UWIDTH == 2)) {
                done = true;
                if (MOVE[j]) {
                  gtable[i][IREP[j]].u   = gtable[i][GGI[j][i] + j].u;
                  gtable[i][IREP[j]].tag = gtable[i][GGI[j][i] + j].tag;
                  gtable[i][IREP[j]].ctr = gtable[i][GGI[j][i] + j].ctr;
                }

                gtable[i][GGI[j][i] + j].tag = GTAG[i];
#ifndef FORCEU
                gtable[i][GGI[j][i] + j].u = 0;
#else

                gtable[i][GGI[j][i] + j].u = ((TAHEAD_UWIDTH == 2) || (TICKH >= BORNTICK / 2)) & (First ? 1 : 0);
#endif
                gtable[i][GGI[j][i] + j].ctr = (resolveDir) ? 0 : -1;

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
#ifdef FORCEU

            for (int jj = 0; jj < ASSOC; jj++) {
              {
                // some just allocated entries  have been set to useful
                if ((MYRANDOM() & ((1 << (1 + LOGASSOC + REPSK)) - 1)) == 0) {
                  if (abs(2 * gtable[i][GGI[jj][i] + jj].ctr + 1) == 1) {
                    if (gtable[i][GGI[jj][i] + jj].u == 1) {
                      gtable[i][GGI[jj][i] + jj].u--;
                    }
                  }
                }
                if (REPSK == 1) {
                  if ((MYRANDOM() & ((1 << (1 + LOGASSOC + REPSK)) - 1)) == 0) {
                    if (abs(2 * gtable[i][IREP[jj]].ctr + 1) == 1) {
                      if (gtable[i][IREP[jj]].u == 1) {
                        gtable[i][IREP[jj]].u--;
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

// we set two counts to monitor: "time to reset u" and "almost time reset u": TICKH is useful only if TAHEAD_UWIDTH =1
#ifndef PROTECTRECENTALLOCUSEFUL
      TICKH += Penalty - NA;
      TICK += Penalty - 2 * NA;
#else
      TICKH += 2 * Penalty - 3 * NA;
      TICK += Penalty - (2 + 2 * (CountMiss11 >= 0)) * NA;
#endif
      if (TICKH < 0) {
        TICKH = 0;
      }
      if (TICKH >= BORNTICK) {
        TICKH = BORNTICK;
      }

      if (TICK < 0) {
        TICK = 0;
      }
      if (TICK >= BORNTICK) {
#ifndef TAHEAD_INTERLEAVED
        // the simulator was designed for NHIST= 14
        if (NHIST == 14) {
          for (int i = 1; i <= ((SHARED) ? 8 : 14); i++) {
            for (int j = 0; j < ASSOC * (1 << (TAHEAD_LOGG + (SHARED ? (i <= 6) : 0))); j++) {
              // this is not realistic: in a real processor:    gtable[1][j].u >>= 1;
              if (gtable[i][j].u > 0) {
                gtable[i][j].u--;
              }
            }
          }
        }

        else {
          for (int i = 1; i <= NHIST; i++) {
            for (int j = 0; j < ASSOC * (1 << TAHEAD_LOGG); j++) {
              // this is not realistic: in a real processor:    gtable[1][j].u >>= 1;
              if (gtable[i][j].u > 0) {
                gtable[i][j].u--;
              }
            }
          }
        }
#else

        for (int j = 0; j < ASSOC * (1 << TAHEAD_LOGG) * NHIST; j++) {
          // this is not realistic: in a real processor:    gtable[1][j].u >>= 1;
          if (gtable[1][j].u > 0) {
            gtable[1][j].u--;
          }
        }
#endif
        TICK  = 0;
        TICKH = 0;
      }
    }

    // update TAGE predictions

    if (HitBank > 0) {
#ifdef UPDATEALTONWEAKMISP
      // This protection, when prediction is low confidence
      if (TAGECONF == 0) {
        if (LongestMatchPred != resolveDir) {
          if (AltBank != HCpredBank) {
            ctrupdate(gtable[AltBank][GGI[AltAssoc][AltBank] + AltAssoc].ctr, resolveDir, TAHEAD_CWIDTH);
          }
          if (HCpredBank > 0) {
            ctrupdate(gtable[HCpredBank][GGI[HCpredAssoc][HCpredBank] + HCpredAssoc].ctr, resolveDir, TAHEAD_CWIDTH);

          }

          else {
            baseupdate(resolveDir);
          }
        }
      }

#endif
      ctrupdate(gtable[HitBank][GGI[HitAssoc][HitBank] + HitAssoc].ctr, resolveDir, TAHEAD_CWIDTH);

    } else {
      baseupdate(resolveDir);
    }
    ////////: note that here it is alttaken that is used: the second hitting entry

    if (LongestMatchPred != alttaken) {
      if (LongestMatchPred == resolveDir) {
#ifdef PROTECTRECENTALLOCUSEFUL

        if (gtable[HitBank][GGI[HitAssoc][HitBank] + HitAssoc].u == 0) {
          gtable[HitBank][GGI[HitAssoc][HitBank] + HitAssoc].u++;
        }
        // Recent useful will survive a smart reset
#endif
        if (gtable[HitBank][GGI[HitAssoc][HitBank] + HitAssoc].u < (1 << TAHEAD_UWIDTH) - 1) {
          gtable[HitBank][GGI[HitAssoc][HitBank] + HitAssoc].u++;
        }

      } else {
        if (gtable[HitBank][GGI[HitAssoc][HitBank] + HitAssoc].u > 0) {
          if (predSC == resolveDir) {
            gtable[HitBank][GGI[HitAssoc][HitBank] + HitAssoc].u--;
          };
        }
      }
    }

#ifdef TAHEAD_DELAY_UPDATE
    (void)PCBRANCH;
    (void)predDir;
    (void)opType;
    (void)branchTarget;
    Numero++;
#else
    HistoryUpdate(PCBRANCH, opType, resolveDir, branchTarget, ptghist, tahead_ch_i, ch_t[0], ch_t[1]);
#endif
  }

  void TrackOtherInst(uint64_t PCBRANCH, Opcode opType, bool taken, uint64_t branchTarget) {
#ifdef TAHEAD_DELAY_UPDATE
    (void)PCBRANCH;
    (void)opType;
    (void)taken;
    (void)branchTarget;
    Numero++;
#else
    HistoryUpdate(PCBRANCH, opType, taken, branchTarget, ptghist, tahead_ch_i, ch_t[0], ch_t[1]);
#endif
  }
#ifdef TAHEAD_DELAY_UPDATE
  void delayed_history(uint64_t PCBRANCH, Opcode opType, bool taken, uint64_t branchTarget) {
    HistoryUpdate(PCBRANCH, opType, taken, branchTarget, ptghist, tahead_ch_i, ch_t[0], ch_t[1]);
  }
#endif
};

#endif
