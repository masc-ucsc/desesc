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
  int32_t nUsers;

protected:
  Stats_avg avgTime;

public:
  explicit PortGeneric(const std::string &name);
  virtual ~PortGeneric();

  void subscribe() { nUsers++; }
  void unsubscribe() { nUsers--; }

  TimeDelta_t nextSlotDelta(bool en) { return nextSlot(en) - globalClock; }
  //! occupy a time slot in the port.
  //! Returns when the slot started to be occupied
  virtual Time_t nextSlot(bool en) = 0;

  //! occupy the port for a number of slots.
  //! Returns the time that the first slot was allocated.
  //!
  //! This function is equivalent to:
  //! Time_t t = nextSlot();
  //! for(int32_t i=1;i<nSlots;i++) {
  //!  nextSlot();
  //! }
  //! return t;

  //! returns when the next slot can be free without occupying any slot
  [[nodiscard]] virtual bool is_busy_for(TimeDelta_t clk) const = 0;

  static PortGeneric *create(const std::string &name, NumUnits_t nUnits);
  void                destroy();
};

class PortUnlimited : public PortGeneric {
private:
public:
  explicit PortUnlimited(const std::string &name);

  Time_t             nextSlot(bool en) override;
  [[nodiscard]] bool is_busy_for(TimeDelta_t clk) const override;
};

class PortFullyPipe : public PortGeneric {
private:
  // lTime is the cycle in which the latest use began
  Time_t lTime;

public:
  explicit PortFullyPipe(const std::string &name);

  Time_t             nextSlot(bool en) override;
  [[nodiscard]] bool is_busy_for(TimeDelta_t clk) const override;
};

class PortFullyNPipe : public PortGeneric {
private:
  const NumUnits_t nUnitsMinusOne;
  NumUnits_t       freeUnits;
  Time_t           lTime;

public:
  PortFullyNPipe(const std::string &name, NumUnits_t nFU);

  Time_t             nextSlot(bool en) override;
  [[nodiscard]] bool is_busy_for(TimeDelta_t clk) const override;
};
