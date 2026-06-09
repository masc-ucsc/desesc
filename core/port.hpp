// See LICENSE for details.

#pragma once

#include <functional>
#include <queue>

#include "iassert.hpp"
#include "snippets.hpp"
#include "stats.hpp"

using NumUnits_t = uint16_t;

// Request placed on a priority-managed port. Lower priority value wins (older dinst ID).
struct PendingRequest {
  Time_t                      priority;
  std::function<void(Time_t)> callback;
  bool                        enable_stats;
  bool                        transient;

  bool operator<(const PendingRequest& other) const {
    return priority > other.priority;  // lowest priority value at top of heap
  }
};

// Generic Port used to model contention.
//
// Two disjoint call styles:
//
//   1) Simple / shared ports (caches, buses, NoC routers):
//      `nextSlot(en)` returns the next available cycle and books it synchronously.
//      Priority-aware methods are unsupported.
//
//   2) Priority-managed, CPU-owned ports (functional units, scheduler window):
//      `schedule(en, priority, transient, cb)` queues a request.  At end of each
//      cycle `EventScheduler::advanceClock` drains every priority-managed port's
//      queue in priority order (lowest dinst ID first) and fires the callback
//      with the allocated cycle.  Transient entries can be rolled back via
//      `flush_transient()` so a squashed speculative path stops charging the
//      port's future occupancy.
//
// A port is constructed in exactly one of the two styles via the `priority_managed`
// flag on `create()`.  Mixing styles on the same port asserts.
class PortGeneric {
protected:
  Stats_avg avgTime;

public:
  explicit PortGeneric(const std::string& name);
  virtual ~PortGeneric() = default;

  // Simple API -------------------------------------------------------------
  virtual Time_t nextSlot(bool en) = 0;
  TimeDelta_t    nextSlotDelta(bool en) { return nextSlot(en) - globalClock; }
  [[nodiscard]] virtual bool is_busy_for(TimeDelta_t clk) const = 0;

  // Priority-managed API ---------------------------------------------------
  // Default implementations reject the call; priority-managed subclasses override.
  virtual void schedule(bool en, Time_t priority, bool transient, std::function<void(Time_t)> cb) {
    (void)en;
    (void)priority;
    (void)transient;
    (void)cb;
    I(0);  // Not a priority-managed port
  }
  virtual void flush_transient() {}    // no-op for simple ports
  virtual void drain_pending() {}      // no-op for simple ports; called each cycle
  [[nodiscard]] virtual bool has_pending() const { return false; }

  static std::shared_ptr<PortGeneric> create(const std::string& name, NumUnits_t nUnits, bool priority_managed = false);
};

// -----------------------------------------------------------------------------
// Simple ports (shared): cache, bus, NoC.
// -----------------------------------------------------------------------------

class PortUnlimited : public PortGeneric {
public:
  explicit PortUnlimited(const std::string& name);

  Time_t             nextSlot(bool en) override;
  [[nodiscard]] bool is_busy_for(TimeDelta_t clk) const override;
};

class PortFullyPipe : public PortGeneric {
  Time_t lTime;

public:
  explicit PortFullyPipe(const std::string& name);

  Time_t             nextSlot(bool en) override;
  [[nodiscard]] bool is_busy_for(TimeDelta_t clk) const override;
};

class PortFullyNPipe : public PortGeneric {
  const NumUnits_t nUnitsMinusOne;
  NumUnits_t       freeUnits;
  Time_t           lTime;

public:
  PortFullyNPipe(const std::string& name, NumUnits_t nFU);

  Time_t             nextSlot(bool en) override;
  [[nodiscard]] bool is_busy_for(TimeDelta_t clk) const override;
};

// -----------------------------------------------------------------------------
// Priority-managed ports (CPU-owned).
// -----------------------------------------------------------------------------

// Unlimited capacity with age-ordered drain at end-of-cycle.
// Every drained grant fires with when == globalClock.
class PortUnlimitedPriority : public PortGeneric {
  std::priority_queue<PendingRequest> queue;

public:
  explicit PortUnlimitedPriority(const std::string& name);

  Time_t             nextSlot(bool en) override;
  [[nodiscard]] bool is_busy_for(TimeDelta_t clk) const override;
  void               schedule(bool en, Time_t priority, bool transient, std::function<void(Time_t)> cb) override;
  void               flush_transient() override;
  void               drain_pending() override;
  [[nodiscard]] bool has_pending() const override { return !queue.empty(); }
};

// N-unit pipelined port with age-ordered drain and no future commitments.
// Each cycle, at most nUnits queued requests are granted (highest priority first);
// the rest wait for the next cycle's drain.  This means arrival in a later cycle
// with higher priority can still beat an older-cycle waiter, matching the "age-fair
// across cycle boundaries" behavior of a real scheduler.
class PortPipePriority : public PortGeneric {
  const NumUnits_t                    nUnits;
  std::priority_queue<PendingRequest> queue;

  // Counter resets each new cycle.  granted_cycle tracks the cycle it's valid for.
  NumUnits_t granted_this_cycle = 0;
  Time_t     granted_cycle      = 0;

  void align_cycle();

public:
  PortPipePriority(const std::string& name, NumUnits_t n);

  Time_t             nextSlot(bool en) override;
  [[nodiscard]] bool is_busy_for(TimeDelta_t clk) const override;
  void               schedule(bool en, Time_t priority, bool transient, std::function<void(Time_t)> cb) override;
  void               flush_transient() override;
  void               drain_pending() override;
  [[nodiscard]] bool has_pending() const override;
};
