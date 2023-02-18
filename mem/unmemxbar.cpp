// See LICENSE for details

#include "unmemxbar.hpp"

#include "config.hpp"
#include "memory_system.hpp"

UnMemXBar::UnMemXBar(Memory_system *current, const std::string &section, const std::string &name)
    /* constructor {{{1 */
    : GXBar(section, name) {
  Xbar_unXbar_balance--;  // decrement balance of XBars
  lower_level = NULL;

  num_banks = Config::get_power2(section, "num_banks");
  LineSize  = Config::get_power2(section, "line_size");

  lower_level = current->declareMemoryObj(section, "lower_level");

  addLowerLevel(lower_level);
  I(current);
}
/* }}} */

void UnMemXBar::doReq(MemRequest *mreq)
/* read if splitter above L1 (down) {{{1 */
{
  router->scheduleReq(mreq);
}
/* }}} */

void UnMemXBar::doReqAck(MemRequest *mreq)
/* req ack (up) {{{1 */
{
  I(!mreq->isHomeNode());

  uint32_t pos = common_addr_hash(mreq->getAddr(), LineSize, num_banks);
  router->scheduleReqAckPos(pos, mreq);
}
/* }}} */

void UnMemXBar::doSetState(MemRequest *mreq)
/* setState (up) {{{1 */
{
  uint32_t pos = common_addr_hash(mreq->getAddr(), LineSize, num_banks);
  router->scheduleSetStatePos(pos, mreq);
}
/* }}} */

void UnMemXBar::doSetStateAck(MemRequest *mreq)
/* setStateAck (down) {{{1 */
{
  router->scheduleSetStateAck(mreq);
}
/* }}} */

void UnMemXBar::doDisp(MemRequest *mreq)
/* disp (down) {{{1 */
{
  router->scheduleDisp(mreq);
}
/* }}} */

bool UnMemXBar::isBusy(Addr_t addr) const
/* always can accept writes {{{1 */
{
  return router->isBusyPos(0, addr);
}
/* }}} */

TimeDelta_t UnMemXBar::ffread(Addr_t addr)
/* fast forward reads {{{1 */
{
  return router->ffread(addr);
}
/* }}} */

TimeDelta_t UnMemXBar::ffwrite(Addr_t addr)
/* fast forward writes {{{1 */
{
  return router->ffwrite(addr);
}
/* }}} */
