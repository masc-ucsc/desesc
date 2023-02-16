// See LICENSE for details.

#pragma once

#include <vector>

#include "callback.hpp"
#include "opcode.hpp"
#include "estl.hpp"
#include "iassert.hpp"
#include "snippets.hpp"

class MemObj;
class MemRequest;

// MsgAction enumerate {{{1
//
// MOESI States:
//
// M: Single copy, memory not consistent
//
// E: Single copy, memory is consistent
//
// I: invalid
//
// S: one of (potentially) several copies. Share does not respond to other bus snoop reads
//
// O: Like shared, but the O is responsible to update memory. If O does
// a write back, it can change to S
//
enum MsgAction { ma_setInvalid, ma_setValid, ma_setDirty, ma_setShared, ma_setExclusive, ma_MMU, ma_VPCWU, ma_MAX };

// }}}

class MRouter {
private:
protected:
  class MemObjHashFunc {
  public:
    size_t operator()(const MemObj *mobj) const {
      HASH<const char *> H;
      return H((const char *)mobj);
    }
  };

  MemObj *self_mobj;

  typedef HASH_MAP<MemObj *, MemObj *, MemObjHashFunc> UPMapType;
  // typedef HASH_MAP<MemObj *, MemObj *> UPMapType;
  UPMapType up_map;

  std::vector<MemObj *> up_node;
  std::vector<MemObj *> down_node;

  void updateRouteTables(MemObj *upmobj, MemObj *const top_node);

public:
  MRouter(MemObj *obj);
  virtual ~MRouter();

  int16_t getCreatorPort(const MemRequest *mreq) const;

  void fillRouteTables();
  void addUpNode(MemObj *upm);
  void addDownNode(MemObj *upm);

  void scheduleReqPos(uint32_t pos, MemRequest *mreq, TimeDelta_t lat = 0);
  void scheduleReq(MemRequest *mreq, TimeDelta_t lat = 0);

  void scheduleReqAck(MemRequest *mreq, TimeDelta_t lat = 0);
  void scheduleReqAckAbs(MemRequest *mreq, Time_t w);
  void scheduleReqAckPos(uint32_t pos, MemRequest *mreq, TimeDelta_t lat = 0);

  void scheduleSetStatePos(uint32_t pos, MemRequest *mreq, TimeDelta_t lat = 0);
  void scheduleSetStateAck(MemRequest *mreq, TimeDelta_t lat = 0);
  void scheduleSetStateAckPos(uint32_t pos, MemRequest *mreq, TimeDelta_t lat = 0);

  void scheduleDispPos(uint32_t pos, MemRequest *mreq, TimeDelta_t lat = 0);
  void scheduleDisp(MemRequest *mreq, TimeDelta_t lat = 0);
  void sendDirtyDisp(Addr_t addr, bool doStats, TimeDelta_t lat = 0);
  void sendCleanDisp(Addr_t addr, bool prefetch, bool doStats, TimeDelta_t lat = 0);

  int32_t sendSetStateOthers(MemRequest *mreq, MsgAction ma, TimeDelta_t lat = 0);
  int32_t sendSetStateOthersPos(uint32_t pos, MemRequest *mreq, MsgAction ma, TimeDelta_t lat = 0);
  int32_t sendSetStateAll(MemRequest *mreq, MsgAction ma, TimeDelta_t lat = 0);

  void tryPrefetch(Addr_t addr, bool doStats, int degree, Addr_t pref_sign, Addr_t pc, CallbackBase *cb = 0);
  void tryPrefetchPos(uint32_t pos, Addr_t addr, int degree, bool doStats, Addr_t pref_sign, Addr_t pc, CallbackBase *cb = 0);

  TimeDelta_t ffread(Addr_t addr);
  TimeDelta_t ffwrite(Addr_t addr);
  TimeDelta_t ffreadPos(uint32_t pos, Addr_t addr);
  TimeDelta_t ffwritePos(uint32_t pos, Addr_t addr);

  bool isBusyPos(uint32_t pos, Addr_t addr) const;

  bool isTopLevel() const { return up_node.empty(); }

  MemObj *getDownNode(int pos = 0) const {
    I(down_node.size() > pos);
    return down_node[pos];
  }
};
