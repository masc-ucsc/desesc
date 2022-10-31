// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "callback.hpp"

EventScheduler::TimedCallbacksQueue EventScheduler::cbQ(256);

Time_t globalClock = 0;
Time_t deadClock   = 0;

void EventScheduler::dump() const {
  I(0);
}
