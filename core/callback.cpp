// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "callback.hpp"

#include "port.hpp"

EventScheduler::TimedCallbacksQueue EventScheduler::cbQ(256);

Time_t globalClock = 0;
Time_t deadClock   = 0;

void EventScheduler::dump() const { I(0); }

void EventScheduler::register_drain_port(PortGeneric* port) { get_drain_ports().push_back(port); }

void EventScheduler::drain_port_queues() {
  for (auto* port : get_drain_ports()) {
    port->drain_pending();
  }
}

bool EventScheduler::any_drain_port_has_pending() {
  for (auto* port : get_drain_ports()) {
    if (port->has_pending()) {
      return true;
    }
  }
  return false;
}
