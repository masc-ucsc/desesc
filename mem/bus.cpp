// See LICENSE for details.

#include "bus.hpp"

#include "config.hpp"

Bus::Bus(Memory_system *current, const std::string &sec, const std::string &n)
    : MemObj(sec, n), delay(Config::get_integer(sec, "delay")) {
  NumUnits_t  num = Config::get_integer(section, "port_num");
  TimeDelta_t occ = Config::get_integer(section, "port_occ");

  dataPort = PortGeneric::create(name + "_data", num, occ);
  cmdPort  = PortGeneric::create(name + "_cmd", num, 1);

  I(current);
  MemObj *lower_level = current->declareMemoryObj(section, "lower_level");
  if (lower_level) {
    addLowerLevel(lower_level);
  }
}
/* }}} */

void Bus::doReq(MemRequest *mreq)
/* forward bus read {{{1 */
{
  TimeDelta_t when = cmdPort->nextSlotDelta(mreq->has_stats()) + delay;
  router->scheduleReq(mreq, when);
}
/* }}} */

void Bus::doDisp(MemRequest *mreq)
/* forward bus read {{{1 */
{
  TimeDelta_t when = dataPort->nextSlotDelta(mreq->has_stats()) + delay;
  router->scheduleDisp(mreq, when);
}
/* }}} */

void Bus::doReqAck(MemRequest *mreq)
/* data is coming back {{{1 */
{
  TimeDelta_t when = dataPort->nextSlotDelta(mreq->has_stats()) + delay;

  if (mreq->isHomeNode()) {
    mreq->ack(when);
    return;
  }

  router->scheduleReqAck(mreq, when);
}
/* }}} */

void Bus::doSetState(MemRequest *mreq)
/* forward set state to all the upper nodes {{{1 */
{
  if (router->isTopLevel()) {
    mreq->convert2SetStateAck(ma_setInvalid, false);  // same as a miss (not sharing here)
    router->scheduleSetStateAck(mreq, 1);
    return;
  }
  router->sendSetStateAll(mreq, mreq->getAction(), delay);
}
/* }}} */

void Bus::doSetStateAck(MemRequest *mreq)
/* forward set state to all the lower nodes {{{1 */
{
  if (mreq->isHomeNode()) {
    mreq->ack();
    return;
  }
  router->scheduleSetStateAck(mreq, delay);
}
/* }}} */

bool Bus::isBusy(Addr_t addr) const {
  (void)addr;
  return false;
}

void Bus::tryPrefetch(Addr_t addr, bool doStats, int degree, Addr_t pref_sign, Addr_t pc, CallbackBase *cb) {
  router->tryPrefetch(addr, doStats, degree, pref_sign, pc, cb);
}

TimeDelta_t Bus::ffread(Addr_t addr) {
  (void)addr;
  return delay;
}

TimeDelta_t Bus::ffwrite(Addr_t addr) {
  (void)addr;
  return delay;
}
