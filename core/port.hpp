// See LICESEN for details.

#pragma once

#include "callback.hpp"
#include "iassert.hpp"
#include "stats.hpp"

using NumUnits_t = uint16_t;

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

  static std::shared_ptr<PortGeneric> create(const std::string& name, NumUnits_t nUnits);
};

class PortUnlimited : public PortGeneric {
private:
public:
  explicit PortUnlimited(const std::string& name);

  Time_t             nextSlot(bool en) override;
  [[nodiscard]] bool is_busy_for(TimeDelta_t clk) const override;
};

class PortFullyPipe : public PortGeneric {
private:
  // lTime is the cycle in which the latest use began
  Time_t lTime;

public:
  explicit PortFullyPipe(const std::string& name);

  Time_t             nextSlot(bool en) override;
  [[nodiscard]] bool is_busy_for(TimeDelta_t clk) const override;
};

class PortFullyNPipe : public PortGeneric {
private:
  const NumUnits_t nUnitsMinusOne;
  NumUnits_t       freeUnits;
  Time_t           lTime;

public:
  PortFullyNPipe(const std::string& name, NumUnits_t nFU);

  Time_t             nextSlot(bool en) override;
  [[nodiscard]] bool is_busy_for(TimeDelta_t clk) const override;
};
