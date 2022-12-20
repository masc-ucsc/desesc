// See LICENSE for details.


#include <math.h>

#include "fmt/format.h"

#include "AccProcessor.h"
#include "GMemorySystem.h"
#include "MemRequest.h"
#include "config.hpp"


AccProcessor::AccProcessor(std::shared_ptr<GMemorySystem> gm, Hartid_t i)
    /* constructor {{{1 */
    : Simu_base(gm, i)
    , myAddr((i + 1) * 128 * 1024)
    , addrIncr(Config::get_integer("soc", "core", i, "addr_incr"))
    , reqid(0)
    , accReads(fmt::format("P({})_acc_reads", i))
    , accWrites(fmt::format("P({})_acc_writes", i))
    , accReadLatency(fmt::format("P({})_acc_ave_read_latency", i))
    , accWriteLatency(fmt::format("P({})_acc_ave_write_latency", i)) {}
/* }}} */

AccProcessor::~AccProcessor()
/* destructor {{{1 */
{
  // Nothing to do
}
/* }}} */

void AccProcessor::read_performed(uint32_t id, Time_t startTime)
// {{{1 callback for completed reads
{
  (void)id;
  accReads.inc(true);
  accReadLatency.sample((int)(globalClock - startTime), true);
}
/* }}} */

void AccProcessor::write_performed(uint32_t id, Time_t startTime)
// {{{1 callback for completed writes
{
  (void)id;
  accWrites.inc(true);
  accWriteLatency.sample((int)(globalClock - startTime), true);
}
/* }}} */

bool AccProcessor::advance_clock_drain() {
  if (is_power_down()) {
    return false;
  }

  adjust_clock(true);

  return true;
}

bool AccProcessor::advance_clock() {
  if (globalClock > 500 && ((globalClock % 10) == (hid))) {
    if (reqid & 1) {
      MemRequest::sendReqWrite(memorySystem->getDL1(),
                               true,
                               myAddr += addrIncr,
                               0,
                               write_performedCB::create(this, reqid++, globalClock));
    } else {
      MemRequest::sendReqRead(memorySystem->getDL1(), true, myAddr, 0, read_performedCB::create(this, reqid++, globalClock));
    }
  }

  return advance_clock_drain();
}
