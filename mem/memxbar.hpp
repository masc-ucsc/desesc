// See LICENSE for details

#pragma once

#include "gxbar.hpp"

class MemXBar : public GXBar {
protected:
  MemObj **lower_level_banks;
  uint32_t num_banks;
  uint32_t dropBits;

  Stats_cntr **XBar_rw_req;

  void init(const std::string &section);

public:
  MemXBar(Memory_system *current, const std::string &device_descr_section, const std::string device_name = "");
  MemXBar(const std::string &section, const std::string &name);
  ~MemXBar() {}

  // Entry points to schedule that may schedule a do?? if needed
  void req(MemRequest *req) { doReq(req); };
  void reqAck(MemRequest *req) { doReqAck(req); };
  void setState(MemRequest *req) { doSetState(req); };
  void setStateAck(MemRequest *req) { doSetStateAck(req); };
  void disp(MemRequest *req) { doDisp(req); }

  // This do the real work
  void doReq(MemRequest *r);
  void doReqAck(MemRequest *req);
  void doSetState(MemRequest *req);
  void doSetStateAck(MemRequest *req);
  void doDisp(MemRequest *req);

  void tryPrefetch(Addr_t addr, bool doStats, int degree, Addr_t pref_sign, Addr_t pc, CallbackBase *cb = 0);

  TimeDelta_t ffread(Addr_t addr);
  TimeDelta_t ffwrite(Addr_t addr);

  bool isBusy(Addr_t addr) const;

  uint32_t addrHash(Addr_t addr) const;
};
