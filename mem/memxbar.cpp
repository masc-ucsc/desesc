// See LICENSE for details.

#include "memxbar.hpp"

#include "absl/strings/str_split.h"
#include "config.hpp"
#include "memory_system.hpp"

MemXBar::MemXBar(const std::string &sec, const std::string &n) : GXBar(sec, n) { /*{{{*/

  init();

} /*}}}*/

MemXBar::MemXBar(Memory_system *current, const std::string &sec, const std::string n)
    /* {{{ constructor */
    : GXBar(sec, n) {
  I(current);
  lower_level_banks = NULL;

  init();

  lower_level_banks = new MemObj *[num_banks];
  XBar_rw_req       = new Stats_cntr *[num_banks];

  std::vector<std::string> vPars = absl::StrSplit(Config::get_string(section, "lower_level"), ' ');
  if (vPars.empty()) {
    Config::add_error(fmt::format("invalid lower_level pointer in section:{}", section));
    return;
  }
  std::string lower_name;
  if (vPars.size() > 1) {
    lower_name = vPars[1];
  }

  for (size_t i = 0; i < num_banks; i++) {
    std::string tmp;
    if (num_banks > 1) {
      tmp = fmt::format("{}{}({})", name, lower_name, i);
    } else {
      tmp = fmt::format("{}{}", name, lower_name);
    }

    lower_level_banks[i] = current->declareMemoryObj_uniqueName(tmp, std::string(vPars[0]));
    addLowerLevel(lower_level_banks[i]);

    XBar_rw_req[i] = new Stats_cntr(fmt::format("{}_to_{}:rw_req", name, lower_level_banks[i]->getName()));
  }
}
/* }}} */

void MemXBar::init() {
  dropBits  = Config::get_integer(section, "drop_bits");
  num_banks = Config::get_power2(section, "num_banks");
}

uint32_t MemXBar::addrHash(Addr_t addr) const {
  addr = addr >> dropBits;
  return (addr % num_banks);
}

void MemXBar::doReq(MemRequest *mreq)
/* read if splitter above L1 (down) {{{1 */
{
  if (mreq->getAddr() == 0) {
    mreq->ack();
    return;
  }

  uint32_t pos = addrHash(mreq->getAddr());
  I(pos < num_banks);

  mreq->resetStart(lower_level_banks[pos]);
  XBar_rw_req[pos]->inc(mreq->has_stats());

  router->scheduleReqPos(pos, mreq);
}
/* }}} */

void MemXBar::doReqAck(MemRequest *mreq)
/* req ack (up) {{{1 */
{
  I(0);

  if (mreq->isHomeNode()) {
    mreq->ack();
    return;
  }
  router->scheduleReqAck(mreq);
}
/* }}} */

void MemXBar::doSetState(MemRequest *mreq)
/* setState (up) {{{1 */
{
  // FIXME
  I(0);  // You should check the L1 as incoherent, so that it does not send invalidated to higher levels
  router->sendSetStateAll(mreq, mreq->getAction());
}
/* }}} */

void MemXBar::doSetStateAck(MemRequest *mreq)
/* setStateAck (down) {{{1 */
{
  uint32_t pos = addrHash(mreq->getAddr());
  router->scheduleSetStateAckPos(pos, mreq);
  // FIXME: use dinst->getPE() to decide who to send up if GPU mode
  // I(0);
}
/* }}} */

void MemXBar::doDisp(MemRequest *mreq)
/* disp (down) {{{1 */
{
  uint32_t pos = addrHash(mreq->getAddr());
  router->scheduleDispPos(pos, mreq);
  I(0);
  // FIXME: use dinst->getPE() to decide who to send up if GPU mode
}
/* }}} */

bool MemXBar::isBusy(Addr_t addr) const
/* always can accept writes {{{1 */
{
  uint32_t pos = addrHash(addr);
  return router->isBusyPos(pos, addr);
}
/* }}} */

void MemXBar::tryPrefetch(Addr_t addr, bool doStats, int degree, Addr_t pref_sign, Addr_t pc, CallbackBase *cb)
/* fast forward reads {{{1 */
{
  uint32_t pos = addrHash(addr);
  router->tryPrefetchPos(pos, addr, degree, doStats, pref_sign, pc, cb);
}
/* }}} */

TimeDelta_t MemXBar::ffread(Addr_t addr)
/* fast forward reads {{{1 */
{
  uint32_t pos = addrHash(addr);
  return router->ffreadPos(pos, addr);
}
/* }}} */

TimeDelta_t MemXBar::ffwrite(Addr_t addr)
/* fast forward writes {{{1 */
{
  uint32_t pos = addrHash(addr);
  return router->ffwritePos(pos, addr);
}
/* }}} */
