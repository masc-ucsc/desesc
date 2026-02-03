// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "callback.hpp"

#ifdef PORT_STRICT_PRIORITY
#include "port.hpp"
#endif

EventScheduler::TimedCallbacksQueue EventScheduler::cbQ(256);

Time_t globalClock = 0;
Time_t deadClock   = 0;

void EventScheduler::dump() const { I(0); }

#ifdef PORT_STRICT_PRIORITY
void EventScheduler::registerPort(PortGeneric* port) {
  getRegisteredPorts().push_back(port);
}

void EventScheduler::processPendingPortRequests() {
  // Process all registered ports' pending requests at start of cycle
  for (auto* port : getRegisteredPorts()) {
    port->processPendingRequests();
  }
}
#endif
