// See LICENSE for details.

#include "Prefetcher.h"

#include <math.h>
#include <stdlib.h>

#include <iostream>

#include "FetchEngine.h"
#include "MemObj.h"
#include "config.hpp"

//#define PREFETCH_HIST 1

Prefetcher::Prefetcher(MemObj *_l1, int hartid)
    /* constructor {{{1 */
    : DL1(_l1)
    , avgPrefetchNum("P(%d)_pref_avgPrefetchNum", hartid)
    , avgPrefetchConf("P(%d)_pref__avgPrefetchConf", hartid)
    , histPrefetchDelta("P(%d)_pref__histPrefetchDelta", hartid)
    , nextPrefetchCB(this) {

  auto section = Config::get_string("soc", "core", hartid, "prefetcher");

  degree = Config::get_integer(section, "degree", 1, 1024);
  distance = Config::get_integer(section, "distance",0, degree);

  pending_prefetch    = false;
  pending_chain_fetch = 0;
  pending_preq_conf   = 0;
  curPrefetch         = 0;

  std::string dl1_section = DL1->getSection();
  int         bsize       = Config::get_integer(dl1_section, "line_size");
  lineSizeBits            = log2i(bsize);

  auto type = Config::get_string(section, "type", {"stride", "indirect", "tage", "void"});

  if (type == "stride") {
    pref_sign = PSIGN_STRIDE;

    apred = new StrideAddressPredictor();
  } else if (type == "indirect") {
    pref_sign = PSIGN_STRIDE;  // STRIDE, Indirect are generated inside the try_chain

    apred = new IndirectAddressPredictor();
  } else if (type == "tage") {
    pref_sign = PSIGN_TAGE;

    auto bimodalSize    = Config::get_power2(section, "bimodal_size", 1);
    auto Log2FetchWidth = log2(Config::get_power2("soc", "core", hartid, "fetch_width"));
    auto bwidth         = Config::get_integer(section, "bimodal_width", 1);
    auto ntables        = Config::get_integer(section, "ntables", 1);

    apred = new vtage(bimodalSize, Log2FetchWidth, bwidth, ntables);
  } else if (type == "void") {
    apred = 0;
  }
}
/* }}} */

void Prefetcher::exe(Dinst *dinst)
/* forward bus read {{{1 */
{
  if (apred == 0)
    return;

  uint16_t conf = apred->exe_update(dinst->getPC(), dinst->getAddr(), dinst->getData());

  GI(!pending_prefetch, pending_preq_conf == 0);

  if (pending_preq_pc == dinst->getPC() && pending_preq_conf > (conf / 2))
    return;  // Do not kill itself

  static uint8_t rnd_xtra_conf = 0;
  rnd_xtra_conf                = 0;  // (rnd_xtra_conf>>1) ^ (dinst->getPC()>>5) ^ (dinst->getAddr()>>2) ^ (dinst->getPC()>>7);
  if (conf <= (pending_preq_conf + (rnd_xtra_conf & 0x7)) || conf <= 4)
    return;  // too low of a chance

  avgPrefetchConf.sample(conf, dinst->getStatsFlag());
  if (pending_prefetch && (curPrefetch - distance) > 0)
    avgPrefetchNum.sample(curPrefetch - distance, pending_statsFlag);

  pending_preq_pc   = dinst->getPC();
  pending_preq_conf = conf;
  pending_statsFlag = dinst->getStatsFlag();
  if (dinst->getChained()) {
    I(dinst->getFetchEngine());
    curPrefetch         = dinst->getChained();
    pending_chain_fetch = dinst->getFetchEngine();
  } else {
    I(!dinst->getFetchEngine());
    curPrefetch         = distance;
    pending_chain_fetch = 0;
  }
  pending_preq_addr = dinst->getAddr();

  if (!pending_prefetch) {
    pending_prefetch = true;
    nextPrefetchCB.schedule(1);
  }
}
/* }}} */

void Prefetcher::ret(Dinst *dinst)
// {{{1 update prefetcher state at retirement
{
  if (apred == 0)
    return;

  int ret = apred->ret_update(dinst->getPC(), dinst->getAddr(), dinst->getData());
  if (ret) {
    dinst->markPrefetch();
  }
}
// 1}}}

void Prefetcher::nextPrefetch()
// {{{1 Method called to trigger a prefetch
{
  I(apred);

  if (!pending_prefetch)
    return;

  if (pending_preq_conf > 0)
    pending_preq_conf--;

  curPrefetch++;

  if (curPrefetch >= degree || pending_preq_conf <= 1) {
    pending_prefetch    = false;
    pending_chain_fetch = 0;
    pending_preq_conf   = 0;
    avgPrefetchNum.sample(curPrefetch - distance - 1, pending_statsFlag);
    return;
  }

  Addr_t paddr;
  if (pending_chain_fetch) {
    paddr = apred->predict(pending_preq_pc, curPrefetch + 4, false);

  } else {
    paddr = apred->predict(pending_preq_pc, curPrefetch + (curPrefetch - distance), true);
  }
  if ((paddr >> 12) == 0) {
    bool chain = apred->try_chain_predict(DL1, pending_preq_pc, curPrefetch + (curPrefetch - distance));
    if (!chain) {
      if ((curPrefetch - distance - 1) > 0)
        avgPrefetchNum.sample(curPrefetch - distance - 1, pending_statsFlag);
      pending_prefetch    = false;
      pending_chain_fetch = 0;
      pending_preq_conf   = 0;
      return;
    }
  } else {
#ifdef PREFETCH_HIST
    histPrefetchDelta.sample(pending_statsFlag, (paddr - pending_preq_addr), 1);
#endif
    pending_preq_addr = paddr;
    CallbackBase *cb  = 0;
    if (pending_chain_fetch) {
      cb = FetchEngine::chainPrefDoneCB::create(pending_chain_fetch, pending_preq_pc, curPrefetch + 4, paddr);
    }
    DL1->tryPrefetch(paddr, pending_statsFlag, curPrefetch, pref_sign, pending_preq_pc, cb);
  }

  if (paddr == pending_preq_addr) {  // Offset 0
    pending_prefetch    = false;
    pending_chain_fetch = 0;
    pending_preq_conf   = 0;
    return;
  }

  I(pending_prefetch);
  nextPrefetchCB.schedule(1);
}
// 1}}}
