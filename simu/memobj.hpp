// See LICENSE for details.

#pragma once

#include <queue>
#include <string>
#include <vector>

#include "callback.hpp"
#include "dinst.hpp"
#include "iassert.hpp"
#include "mrouter.hpp"
#include "port.hpp"

class MemRequest;

#define PSIGN_NONE       0
#define PSIGN_RAS        1
#define PSIGN_NLINE      2
#define PSIGN_STRIDE     3
#define PSIGN_TAGE       4
#define PSIGN_INDIRECT   5
#define PSIGN_CHASE      6
#define PSIGN_MEGA       7
#define LDBUFF_SIZE      512
#define CIR_QUEUE_WINDOW 512  // FIXME: need to change this to a conf variable

#define LOT_QUEUE_SIZE 64  // 512 //FIXME: need to change this to a conf variable
// #define BOT_SIZE 512 //16 //512
// #define LOR_SIZE 512
// #define LOAD_TABLE_SIZE 512 //64 //512
// #define PLQ_SIZE 512 //512
#define LOAD_TABLE_CONF      63
#define LOAD_TABLE_DATA_CONF 63
// #define NUM_FSM_ALU 32

class MemObj {
private:
protected:
  friend class MRouter;

  MRouter*    router;
  std::string section;
  std::string name;
  std::string mem_type;

  const uint16_t  id;
  static uint16_t id_counter;
  int16_t         coreid;
  bool            firstLevelIL1;
  bool            firstLevelDL1;
  bool            isLLC;
  void            addLowerLevel(MemObj* obj);
  void            addUpperLevel(MemObj* obj);

public:
  MemObj(const std::string& section, const std::string& sName);
  MemObj();
  virtual ~MemObj();

  bool isLastLevelCache();

  const std::string& getSection() const { return section; }
  const std::string& getName() const { return name; }
  const std::string& get_type() const { return mem_type; }
  uint16_t           getID() const { return id; }
  int16_t            getCoreID() const { return coreid; }
  void               setCoreDL1(int16_t cid) {
    coreid        = cid;
    firstLevelDL1 = true;
  }
  void setCoreIL1(int16_t cid) {
    coreid        = cid;
    firstLevelIL1 = true;
  }
  bool isFirstLevel() const { return coreid != -1; };
  bool isFirstLevelDL1() const { return firstLevelDL1; };
  bool isFirstLevelIL1() const { return firstLevelIL1; };

  MRouter* getRouter() { return router; }

  virtual void tryPrefetch(Addr_t addr, bool doStats, int degree, Addr_t pref_sign, Addr_t pc, CallbackBase* cb = 0) = 0;

  // Interface for fast-forward (no BW, just warmup caches)
  virtual TimeDelta_t ffread(Addr_t addr)  = 0;
  virtual TimeDelta_t ffwrite(Addr_t addr) = 0;

  // DOWN
  virtual void req(MemRequest* req)         = 0;
  virtual void setStateAck(MemRequest* req) = 0;
  virtual void disp(MemRequest* req)        = 0;

  virtual void doReq(MemRequest* req)         = 0;
  virtual void doSetStateAck(MemRequest* req) = 0;
  virtual void doDisp(MemRequest* req)        = 0;

  // UP
  virtual void blockFill(MemRequest* req);
  virtual void reqAck(MemRequest* req)   = 0;
  virtual void setState(MemRequest* req) = 0;

  virtual void doReqAck(MemRequest* req)   = 0;
  virtual void doSetState(MemRequest* req) = 0;

  virtual bool isBusy(Addr_t addr) const = 0;

  // Print stats
  virtual void dump() const;

  // Optional virtual methods
  virtual bool checkL2TLBHit(MemRequest* req);
  virtual void replayCheckLSQ_removeStore(Dinst*);
  virtual void updateXCoreStores(Addr_t addr);
  virtual void replayflush();
  virtual void plug();

  virtual void setNeedsCoherence();
  virtual void clearNeedsCoherence();

  virtual bool Invalid(Addr_t addr) const;
};

class DummyMemObj : public MemObj {
private:
protected:
public:
  DummyMemObj();
  DummyMemObj(const std::string& section, const std::string& sName);

  // Entry points to schedule that may schedule a do?? if needed
  void req(MemRequest* req) { doReq(req); };
  void reqAck(MemRequest* req) { doReqAck(req); };
  void setState(MemRequest* req) { doSetState(req); };
  void setStateAck(MemRequest* req) { doSetStateAck(req); };
  void disp(MemRequest* req) { doDisp(req); }

  // This do the real work
  void doReq(MemRequest* req);
  void doReqAck(MemRequest* req);
  void doSetState(MemRequest* req);
  void doSetStateAck(MemRequest* req);
  void doDisp(MemRequest* req);

  TimeDelta_t ffread(Addr_t addr);
  TimeDelta_t ffwrite(Addr_t addr);

  void tryPrefetch(Addr_t addr, bool doStats, int degree, Addr_t pref_sign, Addr_t pc, CallbackBase* cb = 0);

  bool isBusy(Addr_t addr) const;
};
