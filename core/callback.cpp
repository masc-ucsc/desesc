// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "callback.hpp"

#include "port.hpp"

EventScheduler::TimedCallbacksQueue EventScheduler::cbQ(256);

Time_t globalClock = 0;
Time_t deadClock   = 0;

void EventScheduler::dump() const { I(0); }

void EventScheduler::registerPort(PortGeneric* port) { getRegisteredPorts().push_back(port); }

void EventScheduler::processPendingPortRequests() {
  // Process all registered ports' pending requests at start of cycle
  // With PORT_STRICT_PRIORITY: priority order (lowest ID first)
  // Without PORT_STRICT_PRIORITY: FIFO order (first inserted, first popped)
  for (auto* port : getRegisteredPorts()) {
    port->processPendingRequests();
  }
}
