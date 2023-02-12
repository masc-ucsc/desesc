// See license.txt for details

#pragma once

#include "stats.hpp"
#include "memobj.hpp"
#include "memrequest.hpp"
#include "memory_system.hpp"
#include "port.hpp"

class Bus : public MemObj {
protected:
  TimeDelta_t delay;
  // Time_t busyUpto;

  PortGeneric *dataPort;
  PortGeneric *cmdPort;

public:
  Bus(MemorySystem *current, const std::string &device_descr_section, const std::string &device_name = NULL);
  ~Bus() {
  }

  // Entry points to schedule that may schedule a do?? if needed
  void req(MemRequest *req) {
    doReq(req);
  };
  void reqAck(MemRequest *req) {
    doReqAck(req);
  };
  void setState(MemRequest *req) {
    doSetState(req);
  };
  void setStateAck(MemRequest *req) {
    doSetStateAck(req);
  };
  void disp(MemRequest *req) {
    doDisp(req);
  }

  // This do the real work
  void doReq(MemRequest *r);
  void doReqAck(MemRequest *req);
  void doSetState(MemRequest *req);
  void doSetStateAck(MemRequest *req);
  void doDisp(MemRequest *req);

  TimeDelta_t ffread(Addr_t addr);
  TimeDelta_t ffwrite(Addr_t addr);

  void tryPrefetch(Addr_t addr, bool doStats, int degree, Addr_t pref_sign, Addr_t pc, CallbackBase *cb = 0);

  bool isBusy(Addr_t addr) const;
};

