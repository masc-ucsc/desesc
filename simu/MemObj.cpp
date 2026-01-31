// See LICENSE for details.

#include "memobj.hpp"

#include <string.h>

#include <set>

#include "absl/strings/str_split.h"
#include "config.hpp"
#include "fmt/format.h"
#include "gmemory_system.hpp"
#include "memrequest.hpp"

uint16_t MemObj::id_counter = 0;

MemObj::MemObj(const std::string& sSection, const std::string& sName)
    /* constructor {{{1 */
    : section(sSection), name(sName), id(id_counter++) {
  mem_type = Config::get_string(section, "type");

  coreid        = -1;  // No first Level cache by default
  firstLevelIL1 = false;
  firstLevelDL1 = false;
  isLLC         = false;

  std::vector<std::string> lower_level = absl::StrSplit(Config::get_string(section, "lower_level"), ' ');
  if (lower_level.empty() || lower_level[0] == "" || lower_level[0] == "void") {
    isLLC = true;
  } else {
    auto lower_level_type = Config::get_string(lower_level[0], "type");
    if (lower_level_type != "cache" && mem_type == "cache") {
      isLLC = true;
    }
  }

  // Create router (different objects may override the default router)
  router = new MRouter(this);

  if (!name.empty()) {
    std::string name_lc(name);
    std::transform(name_lc.begin(), name_lc.end(), name_lc.begin(), [](unsigned char c) { return std::tolower(c); });
    static std::set<std::string> usedNames;
    // Verify that one else uses the same name
    if (usedNames.find(name_lc) != usedNames.end()) {
      Config::add_error(fmt::format("multiple memory objects have same name '{}' (rename one of them)", name_lc));
    } else {
      usedNames.insert(name_lc);
    }
  }
}
/* }}} */

MemObj::~MemObj()
/* destructor {{{1 */
{}
/* }}} */

void MemObj::addLowerLevel(MemObj* obj) {
  router->addDownNode(obj);
  I(obj);
  obj->addUpperLevel(this);
}

void MemObj::addUpperLevel(MemObj* obj) {
  // printf("%s upper level is %s\n",getName(),obj->getName());
  router->addUpNode(obj);
}

void MemObj::blockFill(MemRequest* mreq) {
  (void)mreq;
  // Most objects do nothing
}

bool MemObj::isLastLevelCache() { return isLLC; }

#if 0
void MemObj::tryPrefetch(Addr_t addr, bool doStats, int degree, Addr_t pref_sign, Addr_t pc, CallbackBase *cb)
  /* forward tryPrefetch {{{1 */
{
  router->tryPrefetch(addr,doStats, degree, pref_sign, pc, cb);
}
/* }}} */
#endif

void MemObj::dump() const
/* dump statistics {{{1 */
{
  fmt::print("memObj name [{}]\n", name);
}
/* }}} */

DummyMemObj::DummyMemObj()
    /* dummy constructor {{{1 */
    : MemObj("dummySection", "dummyMem") {}
/* }}} */

DummyMemObj::DummyMemObj(const std::string& _section, const std::string& sName)
    /* dummy constructor {{{1 */
    : MemObj(_section, sName) {}
/* }}} */

void DummyMemObj::doReq(MemRequest* req)
/* req {{{1 */
{
  req->ack();
}
/* }}} */

void DummyMemObj::doReqAck(MemRequest* req)
/* reqAck {{{1 */
{
  (void)req;
  I(0);
}
/* }}} */

void DummyMemObj::doSetState(MemRequest* req)
/* setState {{{1 */
{
  (void)req;
  I(0);
  req->ack();
}
/* }}} */

void DummyMemObj::doSetStateAck(MemRequest* req)
/* setStateAck {{{1 */
{
  (void)req;
  I(0);
}
/* }}} */

void DummyMemObj::doDisp(MemRequest* req)
/* disp {{{1 */
{
  (void)req;
  I(0);
}
/* }}} */

bool DummyMemObj::isBusy(Addr_t addr) const
// Can it accept more requests {{{1
{
  (void)addr;
  return false;
}
// }}}

TimeDelta_t DummyMemObj::ffread(Addr_t addr)
/* fast forward read {{{1 */
{
  (void)addr;
  return 1;  // 1 cycle does everything :)
}
/* }}} */

TimeDelta_t DummyMemObj::ffwrite(Addr_t addr)
/* fast forward write {{{1 */
{
  (void)addr;
  return 1;  // 1 cycle does everything :)
}
/* }}} */

void DummyMemObj::tryPrefetch(Addr_t addr, bool doStats, int degree, Addr_t pref_sign, Addr_t pc, CallbackBase* cb)
/* forward tryPrefetch {{{1 */
{
  (void)addr;
  (void)doStats;
  (void)degree;
  (void)pref_sign;
  (void)pc;
  if (cb) {
    cb->destroy();
  }
}
/* }}} */

/* Optional virtual methods {{{1 */
bool MemObj::checkL2TLBHit(MemRequest* req) {
  // If called, it should be redefined by the object
  (void)req;
  I(0);
  return false;
}
void MemObj::replayCheckLSQ_removeStore(Dinst* dinst) {
  (void)dinst;
  I(0);
}
void MemObj::updateXCoreStores(Addr_t addr) {
  (void)addr;
  I(0);
}
void MemObj::replayflush() { I(0); }
void MemObj::plug() { I(0); }
void MemObj::setNeedsCoherence() {
  // Only cache uses this
}
void MemObj::clearNeedsCoherence() {}

#if 0
bool MemObj::get_cir_queue(int index, Addr_t pc) {
  I(0);
}
#endif

bool MemObj::Invalid(Addr_t addr) const {
  (void)addr;
  I(0);
  return false;
}
/* }}} */
