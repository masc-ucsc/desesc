// See LICESEN for details.

#pragma once

#include "callback.hpp"
#include "iassert.hpp"
#include "stats.hpp"

#include <functional>
#include <queue>

#ifdef PORT_STRICT_PRIORITY
#include <vector>
#endif

using NumUnits_t = uint16_t;

// Request structure for port allocation
struct PendingRequest {
  Time_t                      priority;      // dinst->getID() - lower is higher priority (only used with PORT_STRICT_PRIORITY)
  std::function<void(Time_t)> callback;      // Called when resource allocated
  bool                        enable_stats;  // Whether to track stats

#ifdef PORT_STRICT_PRIORITY
  bool operator<(const PendingRequest& other) const {
    return priority > other.priority;  // Lower ID = higher priority = top of queue
  }
#endif
};

//! Generic Port used to model contention
//! Based on the PortGeneric there are several types of ports.
//! Each has a different algorithm, so that is quite fast.
class PortGeneric {
private:
protected:
  Stats_avg avgTime;

public:
  explicit PortGeneric(const std::string& name);
  virtual ~PortGeneric() = default;

  TimeDelta_t nextSlotDelta(bool en) { return nextSlot(en) - globalClock; }
  //! occupy a time slot in the port.
  //! Returns when the slot started to be occupied
  virtual Time_t nextSlot(bool en) = 0;

  //! returns when the next slot can be free without occupying any slot
  [[nodiscard]] virtual bool is_busy_for(TimeDelta_t clk) const = 0;

  //! Try to allocate a slot with priority. Returns (time, needs_retry).
  //! If needs_retry is false, the slot is allocated at the returned time.
  //! If needs_retry is true, caller should use queueRequest() instead.
  //! Without PORT_STRICT_PRIORITY: always returns needs_retry=false
  virtual std::pair<Time_t, bool> tryNextSlot(bool en, Time_t priority) = 0;

  //! Queue a request for priority-based allocation in future cycles
  //! Without PORT_STRICT_PRIORITY: never called (tryNextSlot never returns needs_retry=true)
  virtual void queueRequest(bool en, Time_t priority, std::function<void(Time_t)> callback) = 0;

  //! Process all pending requests for this cycle (called by EventScheduler)
  //! Without PORT_STRICT_PRIORITY: no-op
  virtual void processPendingRequests() = 0;

  static std::shared_ptr<PortGeneric> create(const std::string& name, NumUnits_t nUnits);
};

class PortUnlimited : public PortGeneric {
private:
public:
  explicit PortUnlimited(const std::string& name);

  Time_t             nextSlot(bool en) override;
  [[nodiscard]] bool is_busy_for(TimeDelta_t clk) const override;

  // PortUnlimited never has contention, so these methods are not needed
  // Provide no-op implementations to satisfy interface
  std::pair<Time_t, bool> tryNextSlot(bool en, Time_t priority) override {
    (void)priority;
    return {nextSlot(en), false};  // Always succeeds immediately
  }
  void queueRequest(bool /*en*/, Time_t /*priority*/, std::function<void(Time_t)> /*callback*/) override {
    I(0);  // Should never be called - tryNextSlot always returns needs_retry=false
  }
  void processPendingRequests() override {
    // No-op: PortUnlimited never queues requests
  }
};

class PortFullyPipe : public PortGeneric {
private:
  // lTime is the cycle in which the latest use began
  Time_t lTime;

#ifdef PORT_STRICT_PRIORITY
  std::priority_queue<PendingRequest> pendingRequests;
#else
  std::queue<PendingRequest> pendingRequests;
#endif

public:
  explicit PortFullyPipe(const std::string& name);

  Time_t             nextSlot(bool en) override;
  [[nodiscard]] bool is_busy_for(TimeDelta_t clk) const override;

  std::pair<Time_t, bool> tryNextSlot(bool en, Time_t priority) override;
  void                    queueRequest(bool en, Time_t priority, std::function<void(Time_t)> callback) override;
  void                    processPendingRequests() override;
};

class PortFullyNPipe : public PortGeneric {
private:
  const NumUnits_t nUnitsMinusOne;
  NumUnits_t       freeUnits;
  Time_t           lTime;

#ifdef PORT_STRICT_PRIORITY
  std::priority_queue<PendingRequest> pendingRequests;
#else
  std::queue<PendingRequest> pendingRequests;
#endif

public:
  PortFullyNPipe(const std::string& name, NumUnits_t nFU);

  Time_t             nextSlot(bool en) override;
  [[nodiscard]] bool is_busy_for(TimeDelta_t clk) const override;

  std::pair<Time_t, bool> tryNextSlot(bool en, Time_t priority) override;
  void                    queueRequest(bool en, Time_t priority, std::function<void(Time_t)> callback) override;
  void                    processPendingRequests() override;
};
