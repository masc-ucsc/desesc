// See LICENSE for details

#pragma once

#include "gxbar.hpp"

class UnMemXBar : public GXBar {
protected:
  MemObj*  lower_level;
  uint32_t num_banks;
  uint32_t LineSize;

public:
  UnMemXBar(Memory_system* current, const std::string& device_descr_section, const std::string& device_name = "");
  ~UnMemXBar() {}

  // Entry points to schedule that may schedule a do?? if needed
  void req(MemRequest* req) { doReq(req); };
  void reqAck(MemRequest* req) { doReqAck(req); };
  void setState(MemRequest* req) { doSetState(req); };
  void setStateAck(MemRequest* req) { doSetStateAck(req); };
  void disp(MemRequest* req) { doDisp(req); }

  // This do the real work
  void doReq(MemRequest* r);
  void doReqAck(MemRequest* req);
  void doSetState(MemRequest* req);
  void doSetStateAck(MemRequest* req);
  void doDisp(MemRequest* req);

  TimeDelta_t ffread(Addr_t addr);
  TimeDelta_t ffwrite(Addr_t addr);

  bool isBusy(Addr_t addr) const;
};
