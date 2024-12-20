// See LICENSE for details.

#pragma once

#include <queue>
#include <vector>

#include "cachecore.hpp"
#include "callback.hpp"
#include "config.hpp"
#include "memory_system.hpp"
#include "memrequest.hpp"
#include "port.hpp"
#include "snippets.hpp"
#include "stats.hpp"

class MemController : public MemObj {
protected:
  class FCFSField {
  public:
    uint32_t    Bank;
    uint32_t    Row;
    uint32_t    Column;
    Time_t      TimeEntered;
    MemRequest *mreq;
  };
  TimeDelta_t delay;
  TimeDelta_t PreChargeLatency;
  TimeDelta_t RowAccessLatency;
  TimeDelta_t ColumnAccessLatency;

  Stats_cntr nPrecharge;
  Stats_cntr nColumnAccess;
  Stats_cntr nRowAccess;
  Stats_avg  avgMemLat;
  Stats_cntr readHit;

  enum STATE {
    IDLE = 0,
    ACTIVATING,
    PRECHARGE,
    ACTIVE,
    ACCESSING,
    INIT  // Added LNB 5/31/2014
  };
  std::shared_ptr<PortGeneric> cmdPort;

  uint32_t rowMask;
  uint32_t columnMask;
  uint32_t bankMask;
  uint32_t rowOffset;
  uint32_t columnOffset;
  uint32_t bankOffset;
  uint32_t numBanks;
  uint32_t memRequestBufferSize;

  class BankStatus {
  public:
    int      state;
    uint32_t activeRow;
    bool     bpend;
    bool     cpend;
    Time_t   bankTime;
  };

  BankStatus *bankState;

  typedef std::vector<FCFSField *> FCFSList;
  FCFSList                         curMemRequests;
  typedef std::queue<FCFSField *>  FCFSQueue;
  FCFSQueue                        OverflowMemoryRequests;

public:
  MemController(Memory_system *current, const std::string &device_descr_section, const std::string &device_name = NULL);
  ~MemController() {}

  // Entry points to schedule that may schedule a do?? if needed
  void req(MemRequest *req) { doReq(req); };
  void reqAck(MemRequest *req) { doReqAck(req); };
  void setState(MemRequest *req) { doSetState(req); };
  void setStateAck(MemRequest *req) { doSetStateAck(req); };
  void disp(MemRequest *req) { doDisp(req); }

  // This do the real work
  void doReq(MemRequest *req);
  void doReqAck(MemRequest *req);
  void doSetState(MemRequest *req);
  void doSetStateAck(MemRequest *req);
  void doDisp(MemRequest *req);

  void tryPrefetch(Addr_t addr, bool doStats, int degree, Addr_t pref_sign, Addr_t pc, CallbackBase *cb = 0);

  TimeDelta_t ffread(Addr_t addr);
  TimeDelta_t ffwrite(Addr_t addr);

  bool isBusy(Addr_t addr) const;

  uint16_t getLineSize() const;

  void manageRam(void);

  typedef CallbackMember0<MemController, &MemController::manageRam> ManageRamCB;
  // typedef CallbackMember0<MemController, &MemController::manageRam>   ManageRamCB;  // Added by LNB 5/27/2014

  // TimeDelta_t ffread(Addr_t addr, DataType data);
  // TimeDelta_t ffwrite(Addr_t addr, DataType data);
  // void        ffinvalidate(Addr_t addr, int32_t lineSize);
private:
  uint32_t getBank(MemRequest *mreq) const;
  uint32_t getRow(MemRequest *mreq) const;
  uint32_t getColumn(MemRequest *mreq) const;
  void     addMemRequest(MemRequest *mreq);

  void transferOverflowMemory(void);
  void scheduleNextAction(void);
};
