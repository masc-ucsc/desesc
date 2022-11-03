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

//#define LARGE_SC
//#define STRICTSIZE
// uncomment to get the 256 Kbits record predictor mentioned in the paper achieves 2.228 MPKI

//#define POSTPREDICT
// uncomment to get a realistic predictor around 256 Kbits , with 12 1024 entries tagged tables in the TAGE predictor, and a global
// history and single local history GEHL statistical corrector total misprediction numbers TAGE-SC-L : 2.435 MPKI TAGE-SC-L  + IMLI:
// 2.294 MPKI TAGE-GSC + IMLI: 2.370 MPKI TAGE-GSC : 2.531 MPKI TAGE alone: 2.602 MPKI

#ifndef _PREDICTOR_H_
#define _PREDICTOR_H_

//#define MEDIUM_TAGE 1
//#define IMLI_150K 1
#define IMLI_256K 1
//#define MEGA_IMLI 1

#if defined(MEGA_IMLI) && defined(MEDIUM_TAGE)
#error "Pick one"
#endif

#define SIMPLER_DOLC_PATH

#ifdef MEDIUM_TAGE
//#define LOOPPREDICTOR //  use loop  predictor
//#define LOCALH			// use local histories
//#define IMLI			// using IMLI component
//#define IMLISIC            //use IMLI-SIC
//#define IMLIOH		//use IMLI-OH
#define LOGG  10 /* logsize of the  tagged TAGE tables*/
#define TBITS 13 /* minimum tag width*/
//#define USE_DOLC 1

#elif MEGA_IMLI        // 1M IMLI
// nhist = 9
#define LOOPPREDICTOR  //  use loop  predictor
#define LOCALH         // use local histories
#define IMLI           // using IMLI component
#define IMLISIC        // use IMLI-SIC
#define IMLIOH         // use IMLI-OH
#define LOGG    12     // logsize of the  tagged TAGE tables
#define TBITS   22     // minimum tag width
#define MAXHIST 400
#define MINHIST 5

#elif IMLI_256K
// nhist = 6
#define LOOPPREDICTOR  //  use loop  predictor
#define LOCALH         // use local histories
#define IMLI           // using IMLI component
#define IMLISIC        // use IMLI-SIC
#define IMLIOH         // use IMLI-OH
#define LOGG    11     // logsize of the  tagged TAGE tables
#define TBITS   16     // minimum tag width
#define MAXHIST 200
#define MINHIST 5

#elif IMLI_150K
// nhist = 4
#define LOOPPREDICTOR  //  use loop  predictor
#define LOCALH         // use local histories
#define IMLI           // using IMLI component
#define IMLISIC        // use IMLI-SIC
#define IMLIOH         // use IMLI-OH
#define LOGG    11     // 11       // logsize of the  tagged TAGE tables
#define TBITS   13     // 16      // minimum tag width
#define MAXHIST 160
#define MINHIST 5
#else
// nhist = 7, glength
#define LOOPPREDICTOR  //  use loop  predictor
#define LOCALH         // use local histories
#define IMLI           // using IMLI component
#define IMLISIC        // use IMLI-SIC
#define IMLIOH         // use IMLI-OH
#define LOGG    12     /* logsize of the  tagged TAGE tables*/
#define TBITS   13     /* minimum tag width*/
#define MAXHIST 200    // 200
#define MINHIST 5
#endif

/*
#ifdef MEGA_IMLI
// use 20 tables (nhist = 20)
#define LOOPPREDICTOR //  use loop  predictor
#define LOCALH        // use local histories
#define IMLI          // using IMLI component
#define IMLISIC       // use IMLI-SIC
#define IMLIOH        // use IMLI-OH
#define LOGG 13       // logsize of the  tagged TAGE tables
#define TBITS 14      // minimum tag width
#define MAXHIST 400
#define MINHIST 5
#endif
*/
// To get the predictor storage budget on stderr  uncomment the next line
#include <assert.h>
#include <inttypes.h>
#include <math.h>

#include <vector>

#include "DOLC.h"
#include "opcode.hpp"

#define SUBENTRIES 1

#define UWIDTH 1
#define CWIDTH 3

// use geometric history length
#ifndef MAXHIST
#ifdef USE_DOLC
#define MAXHIST 71
#define MINHIST 5
#else
//#define MINHIST 7
//#define MAXHIST 1000
#define MINHIST 1
#define MAXHIST 71
#endif
#endif
// probably not the best history length, but nice

#ifdef USE_DOLC
DOLC idolc(MAXHIST, 1, 6, 18);
#endif

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
int Gm[GNB] = {17, 14};
#else
#ifdef LARGE_SC
#define GNB 4
int Gm[GNB] = {27, 22, 17, 14};
#else
#define GNB 2
int Gm[GNB] = {17, 14};
#endif
#endif
#else
#ifdef LARGE_SC
#define GNB 4
int Gm[GNB] = {27, 22, 17, 14};
#else
#define GNB 2
int Gm[GNB] = {17, 14};
#endif

#endif
/*effective length is  -11,  we use (GHIST<<11)+IMLIcount; we force the IMLIcount zero when IMLI is not used*/

int8_t  GGEHLA[GNB][(1 << LOGGNB)];
int8_t *GGEHL[GNB];

// large local history
#define LOGLOCAL 8
#define NLOCAL   (1 << LOGLOCAL)
#define INDLOCAL (PC & (NLOCAL - 1))
#ifdef LARGE_SC
// three different local histories (just completely crazy :-)

#define LOGLNB 10
#define LNB    3
int     Lm[LNB] = {11, 6, 3};
int8_t  LGEHLA[LNB][(1 << LOGLNB)];
int8_t *LGEHL[LNB];
#else
// only one local history
#define LOGLNB 10
#define LNB    4
int     Lm[LNB] = {16, 11, 6, 3};
int8_t  LGEHLA[LNB][(1 << LOGLNB)];
int8_t *LGEHL[LNB];
#endif

// small local history
#define LOGSECLOCAL 4
#define NSECLOCAL   (1 << LOGSECLOCAL)  // Number of second local histories
#define INDSLOCAL   (((PC ^ (PC >> 5))) & (NSECLOCAL - 1))
#define LOGSNB      9
#define SNB         4
int     Sm[SNB] = {16, 11, 6, 3};
int8_t  SGEHLA[SNB][(1 << LOGSNB)];
int8_t *SGEHL[SNB];

// third local history
#define LOGTNB 9
#ifdef STRICTSIZE
#define TNB 2
int Tm[TNB] = {17, 14};
#else
#define TNB 3
int     Tm[TNB] = {22, 17, 14};
#endif
// effective local history size +11: we use IMLIcount + (LH) << 11
int8_t  TGEHLA[TNB][(1 << LOGTNB)];
int8_t *TGEHL[TNB];
#define INDTLOCAL (((PC ^ (PC >> 3))) & (NSECLOCAL - 1))  // different hash for the 3rd history

long long L_shist[NLOCAL];
long long S_slhist[NSECLOCAL];
long long T_slhist[NSECLOCAL];
long long HSTACK[16];
int       pthstack;
#ifdef LARGE_SC
// return-stack associated history component
#ifdef STRICTSIZE
#define LOGPNB 8
#else
#define LOGPNB 9
#endif
#define PNB 4
int     Pm[PNB] = {16, 11, 6, 3};
int8_t  PGEHLA[PNB][(1 << LOGPNB)];
int8_t *PGEHL[PNB];
#else
