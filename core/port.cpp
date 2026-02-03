// See LICENSE for details.

#include "port.hpp"

PortGeneric::PortGeneric(const std::string& name) : avgTime(name) {}

std::shared_ptr<PortGeneric> PortGeneric::create(const std::string& unitName, NumUnits_t nUnits) {
  // Search for the configuration with the best performance. In theory
  // everything can be solved with PortNPipe, but it is the slowest.
  // Sorry, but I'm a performance freak!

  auto name = fmt::format("{}_occ", unitName);

  std::shared_ptr<PortGeneric> gen;

  if (nUnits == 0) {
    gen = std::make_shared<PortUnlimited>(name);
  } else {
    if (nUnits == 1) {
      gen = std::make_shared<PortFullyPipe>(name);
#ifdef PORT_STRICT_PRIORITY
      EventScheduler::registerPort(gen.get());
#endif
    } else {
      gen = std::make_shared<PortFullyNPipe>(name, nUnits);
#ifdef PORT_STRICT_PRIORITY
      EventScheduler::registerPort(gen.get());
#endif
    }
  }

  return gen;
}

PortUnlimited::PortUnlimited(const std::string& name) : PortGeneric(name) {}

Time_t PortUnlimited::nextSlot(bool en) {
  avgTime.sample(0, en);  // Just to keep usage statistics
  return globalClock;
}

bool PortUnlimited::is_busy_for(TimeDelta_t clk) const {
  (void)clk;
  return false;
}

PortFullyPipe::PortFullyPipe(const std::string& name) : PortGeneric(name) { lTime = globalClock; }

Time_t PortFullyPipe::nextSlot(bool en) {
  if (lTime < globalClock) {
    lTime = globalClock;
  }

  avgTime.sample(lTime - globalClock, en);
  return lTime++;
}

bool PortFullyPipe::is_busy_for(TimeDelta_t clk) const { return lTime - clk >= globalClock; }

#ifdef PORT_STRICT_PRIORITY
std::pair<Time_t, bool> PortFullyPipe::tryNextSlot(bool en, Time_t priority) {
  (void)priority;  // Priority is used for queuing, not immediate allocation

  if (lTime < globalClock) {
    lTime = globalClock;
  }

  bool resource_available = (lTime == globalClock);

  if (resource_available) {
    // Resource available this cycle - allocate immediately
    lTime++;
    avgTime.sample(0, en);
    return {globalClock, false};
  } else {
    // Resource busy - caller should queue for retry
    return {lTime, true};
  }
}

void PortFullyPipe::queueRequest(bool en, Time_t priority, std::function<void(Time_t)> callback) {
  pendingRequests.push({priority, callback, en});
}

void PortFullyPipe::processPendingRequests() {
  // Called at start of each cycle - process queued requests in priority order
  while (!pendingRequests.empty()) {
    if (lTime < globalClock) {
      lTime = globalClock;
    }

    // Check if we can allocate this cycle
    if (lTime > globalClock) {
      // All remaining requests must wait for future cycles
      break;
    }

    auto req = pendingRequests.top();
    pendingRequests.pop();

    Time_t when = lTime++;
    avgTime.sample(when - globalClock, req.enable_stats);
    req.callback(when);
  }
}
#endif

PortFullyNPipe::PortFullyNPipe(const std::string& name, NumUnits_t nFU) : PortGeneric(name), nUnitsMinusOne(nFU - 1) {
  I(nFU > 0);  // For unlimited resources use the FUUnlimited

  lTime = globalClock;

  freeUnits = nFU;
}

Time_t PortFullyNPipe::nextSlot(bool en) {
  if (lTime < globalClock) {
    lTime     = globalClock;
    freeUnits = nUnitsMinusOne;
  } else if (freeUnits > 0) {
    freeUnits--;
  } else {
    lTime++;
    freeUnits = nUnitsMinusOne;
  }

  avgTime.sample(lTime - globalClock, en);
  return lTime;
}

bool PortFullyNPipe::is_busy_for(TimeDelta_t clk) const { return lTime - clk >= globalClock; }

#ifdef PORT_STRICT_PRIORITY
std::pair<Time_t, bool> PortFullyNPipe::tryNextSlot(bool en, Time_t priority) {
  (void)priority;  // Priority is used for queuing, not immediate allocation

  if (lTime < globalClock) {
    lTime     = globalClock;
    freeUnits = nUnitsMinusOne;
  }

  bool resource_available = (lTime == globalClock && freeUnits > 0);

  if (resource_available) {
    // Allocate immediately
    freeUnits--;
    avgTime.sample(0, en);
    return {lTime, false};
  } else {
    // Defer to queue
    return {lTime, true};
  }
}

void PortFullyNPipe::queueRequest(bool en, Time_t priority, std::function<void(Time_t)> callback) {
  pendingRequests.push({priority, callback, en});
}

void PortFullyNPipe::processPendingRequests() {
  // Called at start of each cycle - process queued requests in priority order
  while (!pendingRequests.empty()) {
    // Refresh available resources at start of cycle if needed
    if (lTime < globalClock) {
      lTime     = globalClock;
      freeUnits = nUnitsMinusOne;
    }

    // Check if we can allocate this cycle
    if (lTime > globalClock && freeUnits == nUnitsMinusOne) {
      // Port is already scheduled for future cycles, stop processing
      break;
    }

    auto req = pendingRequests.top();
    pendingRequests.pop();

    Time_t when = lTime;
    if (freeUnits > 0) {
      freeUnits--;
    } else {
      lTime++;
      freeUnits = nUnitsMinusOne;
    }

    avgTime.sample(when - globalClock, req.enable_stats);
    req.callback(when);
  }
}
#endif
