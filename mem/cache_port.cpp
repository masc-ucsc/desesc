// See LICENSE for details.

#include "cache_port.hpp"

#include "config.hpp"

Cache_port::Cache_port(const std::string &section, const std::string &name) {
  int numPorts = Config::get_integer(section, "port_num");
  int portOccp = Config::get_integer(section, "port_occ");

  hitDelay  = Config::get_integer(section, "delay", 1, 1024);
  missDelay = Config::get_integer(section, "miss_delay", 1, 1024);

  if (Config::has_entry(section, "nc_miss_delay")) {
    ncDelay = Config::get_integer(section, "nc_miss_delay");
  } else {
    ncDelay = missDelay;
  }

  dataDelay = hitDelay - missDelay;
  tagDelay  = hitDelay - dataDelay;

  numBanks             = Config::get_power2(section, "port_banks", 0, 1024);
  int32_t log2numBanks = log2i(numBanks);
  if (numBanks > 1) {
    numBanksMask = (1 << log2numBanks) - 1;
  } else {
    numBanksMask = 0;
  }

  bkPort = new PortGeneric *[numBanks];
  for (uint32_t i = 0; i < numBanks; i++) {
    bkPort[i] = PortGeneric::create(fmt::format("{}_bk({})", name, i), numPorts, portOccp);
    I(bkPort[i]);
  }
  I(bkPort[0]);
  {
    int send_port_occ = 1;
    int send_port_num = 1;
    if (Config::has_entry(section, "send_port_occ")) {
      send_port_num = Config::get_integer(section, "send_port_num");
      send_port_occ = Config::get_integer(section, "send_port_occ");
    }
    sendFillPort = PortGeneric::create(fmt::format("{}_sendFill", name), send_port_num, send_port_occ);
  }

  maxRequests = Config::get_integer(section, "max_requests");
  if (maxRequests == 0) {
    maxRequests = 32768;  // It should be enough
  }

  maxPrefetch = 32;  // by default share with normal prefetch
  if (Config::has_entry(section, "max_prefetch")) {
    maxPrefetch = Config::get_integer(section, "max_prefetch");
  }
  dropPrefetchFill = true;
  if (Config::has_entry(section, "drop_prefetch")) {
    dropPrefetchFill = Config::get_bool(section, "drop_prefetch");
  }

  curRequests = 0;
  curPrefetch = 0;

  lineSize = Config::get_power2(section, "line_size");
  if (Config::has_entry(section, "bank_shift")) {
    bankShift = Config::get_integer(section, "bank_shift");
    bankSize  = 1 << bankShift;
  } else {
    bankShift = log2i(lineSize);
    bankSize  = lineSize;
  }
  if (Config::has_entry(section, "fill_line_size")) {
    fill_line_size = Config::get_power2(section, "fill_line_size", 1, lineSize);
  } else {
    fill_line_size = lineSize;
  }

  blockTime = 0;
}

Time_t Cache_port::nextBankSlot(Addr_t addr, bool en) {
  int32_t bank = (addr >> bankShift) & numBanksMask;

  return bkPort[bank]->nextSlot(en);
}

Time_t Cache_port::calcNextBankSlot(Addr_t addr) {
  int32_t bank = (addr >> bankShift) & numBanksMask;

  return bkPort[bank]->calcNextSlot();
}

void Cache_port::nextBankSlotUntil(Addr_t addr, Time_t until, bool en) {
  (void)en;  // no stats tracking
  uint32_t bank = (addr >> bankShift) & numBanksMask;

  bkPort[bank]->occupyUntil(until);
}

Time_t Cache_port::reqDone(MemRequest *mreq, bool retrying) {
  if (mreq->isWarmup() || mreq->isDropped()) {
    return globalClock + 1;
  }

  if (dropPrefetchFill && mreq->isPrefetch() && sendFillPort->calcNextSlot() > (globalClock + 8)) {
    mreq->setDropped();
    return globalClock + 1;
  }

  Time_t when = sendFillPort->nextSlot(mreq->has_stats());

  if (!retrying && !mreq->isNonCacheable()) {
    when += dataDelay;
  }

  return when;
}

Time_t Cache_port::reqAckDone(MemRequest *mreq) {
  if (mreq->isWarmup() || mreq->isDropped()) {
    return globalClock + 1;
  }

  if (dropPrefetchFill && mreq->isPrefetch() && sendFillPort->calcNextSlot() > (globalClock + 8)) {
    mreq->setDropped();
    return globalClock + 1;
  }

  Time_t when = sendFillPort->nextSlot(mreq->has_stats());  // tag access simultaneously, no charge here

  return when + 1;
}

bool Cache_port::isBusy(Addr_t addr) const {
  (void)addr;
  if (curRequests >= (maxRequests / 2)) {  // Reserve half for reads which do not check isBusy
    return true;
  }

  return false;
}

void Cache_port::startPrefetch(MemRequest *mreq) {
  I(mreq->isPrefetch());
  I(!mreq->isDropped());

  if (maxPrefetch) {
    curPrefetch++;
  } else {
    curRequests++;
  }
}

void Cache_port::reqRetire(MemRequest *mreq) {
  if (mreq->isPrefetch() && maxPrefetch) {
    curPrefetch--;
  } else {
    curRequests--;
  }
  I(curRequests >= 0);
  I(curPrefetch >= 0);

  GI(curPrefetch, maxPrefetch);  // curPrefech == 0 unless maxPrefetch

  while (!overflow.empty()) {
    MemRequest *oreq = overflow.back();
    overflow.pop_back();
    req2(oreq);
    if (curRequests >= maxRequests) {
      break;
    }
    if (curPrefetch > maxPrefetch) {
      break;
    }
  }
}

void Cache_port::req2(MemRequest *mreq) {
  // I(curRequests<=maxRequests && !mreq->isWarmup());

  I(!mreq->isDropped());

  if (!mreq->isRetrying()) {
    if (mreq->isPrefetch() && maxPrefetch) {
      curPrefetch++;
    } else {
      curRequests++;
    }
  }

  if (mreq->isWarmup()) {
    mreq->redoReq();
  } else if (mreq->isNonCacheable()) {
    mreq->redoReqAbs(globalClock + ncDelay);
  } else {
    mreq->redoReqAbs(nextBankSlot(mreq->getAddr(), mreq->has_stats()) + tagDelay);
  }
}
void Cache_port::req(MemRequest *mreq)
/* main processor read entry point {{{1 */
{
  if (!mreq->isRetrying() && !mreq->isPrefetch()) {
    if (curRequests >= maxRequests) {
      overflow.push_front(mreq);
      return;
    }
    while (!overflow.empty()) {
      MemRequest *oreq = overflow.back();
      overflow.pop_back();
      req2(oreq);
      if (curRequests >= maxRequests) {
        break;
      }
      if (curPrefetch > maxPrefetch) {
        break;
      }
    }
    if (!overflow.empty()) {
      overflow.push_front(mreq);
      return;
    }
  }

  req2(mreq);
}
// }}}

Time_t Cache_port::snoopFillBankUse(MemRequest *mreq) {
  if (mreq->isNonCacheable()) {
    return globalClock;
  }

  if (mreq->isPrefetch() && mreq->isDropped()) {
    return globalClock;
  }

  Time_t max    = globalClock;
  Time_t max_fc = 0;
  for (uint32_t fc = 0; fc < lineSize; fc += fill_line_size) {
    for (uint32_t i = 0; i < fill_line_size; i += bankSize) {
      Time_t t = nextBankSlot(mreq->getAddr() + fc + i, mreq->has_stats());
      if ((t + max_fc) > max) {
        max = t + max_fc;
      }
    }
    max_fc++;
  }

#if 0
  // Make sure that all the banks are busy until the max time
  Time_t cur_fc = 0;
  for(int fc = 0; fc<lineSize ;  fc += fill_line_size) {
    cur_fc++;
    for(int i = 0;i<fill_line_size;i += bankSize) {
      nextBankSlotUntil(mreq->getAddr()+fc+i,max-max_fc+cur_fc, mreq->has_stats());
    }
  }
#endif

  return max;
}

void Cache_port::blockFill(MemRequest *mreq)
// Block the cache ports for fill requests {{{1
{
  if (mreq->isDropped()) {
    return;
  }

  if (dropPrefetchFill && mreq->isPrefetch() && blockTime > (globalClock + 8)) {
    mreq->setDropped();
    return;
  }

  blockTime = snoopFillBankUse(mreq);
}
// }}}

void Cache_port::reqAck(MemRequest *mreq)
/* request Ack {{{1 */
{
  Time_t until;
  if (dropPrefetchFill && mreq->isPrefetch() && blockTime > (globalClock + 8)) {
    mreq->setDropped();
    until = globalClock + 1;
  } else if (mreq->isWarmup() || mreq->isDropped()) {
    until = globalClock + 1;
  } else {
    until = snoopFillBankUse(mreq);
  }

  blockTime = until;

  mreq->redoReqAckAbs(until);
}
// }}}

void Cache_port::setState(MemRequest *mreq)
/* set state {{{1 */
{
  mreq->redoSetStateAbs(globalClock + 1);
}
// }}}

void Cache_port::setStateAck(MemRequest *mreq)
/* set state ack {{{1 */
{
  mreq->redoSetStateAckAbs(globalClock + 1);
}
// }}}

void Cache_port::disp(MemRequest *mreq)
/* displace a CCache line {{{1 */
{
  Time_t t  = snoopFillBankUse(mreq);
  blockTime = t;
  mreq->redoDispAbs(t);
}
// }}}
