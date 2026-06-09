// See LICENSE for details.

#include "port.hpp"

#include <utility>

#include "callback.hpp"
#include "fmt/format.h"

PortGeneric::PortGeneric(const std::string& name) : avgTime(name) {}

std::shared_ptr<PortGeneric> PortGeneric::create(const std::string& unitName, NumUnits_t nUnits, bool priority_managed) {
  auto name = fmt::format("{}_occ", unitName);

  std::shared_ptr<PortGeneric> gen;

  if (nUnits == 0) {
    if (priority_managed) {
      gen = std::make_shared<PortUnlimitedPriority>(name);
      EventScheduler::register_drain_port(gen.get());
    } else {
      gen = std::make_shared<PortUnlimited>(name);
    }
  } else if (priority_managed) {
    gen = std::make_shared<PortPipePriority>(name, nUnits);
    EventScheduler::register_drain_port(gen.get());
  } else if (nUnits == 1) {
    gen = std::make_shared<PortFullyPipe>(name);
  } else {
    gen = std::make_shared<PortFullyNPipe>(name, nUnits);
  }

  return gen;
}

// -----------------------------------------------------------------------------
// PortUnlimited (simple)
// -----------------------------------------------------------------------------

PortUnlimited::PortUnlimited(const std::string& name) : PortGeneric(name) {}

Time_t PortUnlimited::nextSlot(bool en) {
  avgTime.sample(0, en);
  return globalClock;
}

bool PortUnlimited::is_busy_for(TimeDelta_t clk) const {
  (void)clk;
  return false;
}

// -----------------------------------------------------------------------------
// PortFullyPipe (simple, 1 unit)
// -----------------------------------------------------------------------------

PortFullyPipe::PortFullyPipe(const std::string& name) : PortGeneric(name), lTime(globalClock) {}

Time_t PortFullyPipe::nextSlot(bool en) {
  if (lTime < globalClock) {
    lTime = globalClock;
  }
  avgTime.sample(lTime - globalClock, en);
  return lTime++;
}

bool PortFullyPipe::is_busy_for(TimeDelta_t clk) const { return lTime - clk >= globalClock; }

// -----------------------------------------------------------------------------
// PortFullyNPipe (simple, N units)
// -----------------------------------------------------------------------------

PortFullyNPipe::PortFullyNPipe(const std::string& name, NumUnits_t nFU)
    : PortGeneric(name), nUnitsMinusOne(nFU - 1), freeUnits(nFU), lTime(globalClock) {
  I(nFU > 0);
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

// -----------------------------------------------------------------------------
// PortUnlimitedPriority
// -----------------------------------------------------------------------------

PortUnlimitedPriority::PortUnlimitedPriority(const std::string& name) : PortGeneric(name) {}

Time_t PortUnlimitedPriority::nextSlot(bool /*en*/) {
  I(0);  // priority-managed: use schedule()
  return globalClock;
}

bool PortUnlimitedPriority::is_busy_for(TimeDelta_t clk) const {
  (void)clk;
  return false;
}

void PortUnlimitedPriority::schedule(bool en, Time_t priority, bool transient, std::function<void(Time_t)> cb) {
  queue.push(PendingRequest{priority, std::move(cb), en, transient});
}

void PortUnlimitedPriority::flush_transient() {
  if (queue.empty()) {
    return;
  }
  // Rebuild queue without transient entries.
  std::priority_queue<PendingRequest> kept;
  while (!queue.empty()) {
    if (!queue.top().transient) {
      kept.push(queue.top());
    }
    queue.pop();
  }
  queue = std::move(kept);
}

void PortUnlimitedPriority::drain_pending() {
  // Unlimited capacity: every request fires at the current cycle.
  while (!queue.empty()) {
    auto req = queue.top();
    queue.pop();
    avgTime.sample(0, req.enable_stats);
    req.callback(globalClock);
  }
}

// -----------------------------------------------------------------------------
// PortPipePriority (N units, priority-managed, transient-aware)
// -----------------------------------------------------------------------------

PortPipePriority::PortPipePriority(const std::string& name, NumUnits_t n) : PortGeneric(name), nUnits(n) { I(n > 0); }

void PortPipePriority::align_cycle() {
  if (granted_cycle != globalClock) {
    granted_cycle      = globalClock;
    granted_this_cycle = 0;
  }
}

Time_t PortPipePriority::nextSlot(bool /*en*/) {
  I(0);  // priority-managed: use schedule()
  return globalClock;
}

bool PortPipePriority::is_busy_for(TimeDelta_t /*clk*/) const {
  // No future commitments: the port is never "busy for N cycles ahead".
  return false;
}

void PortPipePriority::schedule(bool en, Time_t priority, bool transient, std::function<void(Time_t)> cb) {
  queue.push(PendingRequest{priority, std::move(cb), en, transient});
}

void PortPipePriority::flush_transient() {
  if (queue.empty()) {
    return;
  }
  // Filter out transient entries.  Already-granted transients have already fired and
  // consumed their cycle; nothing to undo because no future cycles were committed.
  std::priority_queue<PendingRequest> kept;
  while (!queue.empty()) {
    if (!queue.top().transient) {
      kept.push(queue.top());
    }
    queue.pop();
  }
  queue = std::move(kept);
}

void PortPipePriority::drain_pending() {
  align_cycle();
  while (!queue.empty() && granted_this_cycle < nUnits) {
    auto req = queue.top();
    queue.pop();
    avgTime.sample(0, req.enable_stats);
    req.callback(globalClock);
    ++granted_this_cycle;
  }
}

bool PortPipePriority::has_pending() const {
  if (queue.empty()) {
    return false;
  }
  // Stale counter from a previous cycle means we haven't granted anything yet this cycle.
  if (granted_cycle != globalClock) {
    return true;
  }
  return granted_this_cycle < nUnits;
}
