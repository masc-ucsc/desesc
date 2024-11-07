// See LICENSE for details.

#pragma once

#include "iassert.hpp"
#include "memobj.hpp"
#include "memrequest.hpp"
#include "port.hpp"

class Cache_port {
private:
protected:
  PortGeneric **bkPort;
  PortGeneric  *sendFillPort;

  bool    dupPrefetchTag;
  bool    dropPrefetchFill;
  int32_t maxPrefetch;  // 0 means share with maxRequest
  int32_t curPrefetch;

  int32_t maxRequests;
  int32_t curRequests;

  TimeDelta_t ncDelay;
  TimeDelta_t hitDelay;
  TimeDelta_t missDelay;

  TimeDelta_t tagDelay;
  TimeDelta_t dataDelay;

  uint32_t numBanks;
  int32_t  numBanksMask;

  uint32_t lineSize;
  uint32_t bankShift;
  uint32_t bankSize;
  uint32_t fill_line_size;

  Time_t blockTime;

  std::list<MemRequest *> overflow;

  Time_t snoopFillBankUse(MemRequest *mreq);

  Time_t nextBankSlot(Addr_t addr, bool en);
  void   req2(MemRequest *mreq);

public:
  Cache_port(const std::string &section, const std::string &name);
  ~Cache_port() = default;

  void   blockFill(MemRequest *mreq);
  void   req(MemRequest *mreq);
  void   startPrefetch(MemRequest *mreq);
  Time_t reqDone(MemRequest *mreq, bool retrying);
  Time_t reqAckDone(MemRequest *mreq);
  void   reqRetire(MemRequest *mreq);

  void reqAck(MemRequest *mreq);
  void setState(MemRequest *mreq);
  void setStateAck(MemRequest *mreq);
  void disp(MemRequest *mreq);

  bool isBusy(Addr_t addr) const;
};
