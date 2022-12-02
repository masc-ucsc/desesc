// See LICESEN for details.

#pragma once

// Clean and Fast interface to implement any kind of contention for a
// bus.  This resource is typically used to model the number of ports
// in a cache, or the occupancy of a functional unit.
//
// A real example of utilization is in Resource.cpp, but this example
// gives you a quick idea:
//
// You want to model the contention for a bus. The bus has an
// occupancy of 2 cycles. To create the port:
//
// PortGeneric *bus = PortGeneric::create("portName",1,2);
// 1 is for the number of busses, 2 for the occupancy.
//
// At any time, you can get queued for the bus, and this structure
// tells you the next cycle when your request would be satisfied.
// Time_t atWhatCycle = bus->nextSlot();
//
// Enjoy!

#include "callback.hpp"
#include "iassert.hpp"
#include "stats.hpp"

typedef uint16_t NumUnits_t;

//! Generic Port used to model contention
//! Based on the PortGeneric there are several types of ports.
//! Each has a different algorithm, so that is quite fast.
class PortGeneric {
private:
  int32_t nUsers;

protected:
  Stats_avg avgTime;

public:
  PortGeneric(const std::string &name);
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
  virtual void occupyUntil(Time_t t);

  //! returns when the next slot can be free without occupying any slot
  virtual Time_t calcNextSlot() const = 0;

  static PortGeneric *create(const std::string &name, NumUnits_t nUnits, TimeDelta_t occ);
  void                destroy();
};

class PortUnlimited : public PortGeneric {
protected:
public:
  PortUnlimited(const std::string &name);

  void   occupyUntil(Time_t t);
  Time_t nextSlot(bool en);
  Time_t calcNextSlot() const;
};

class PortFullyPipe : public PortGeneric {
private:
protected:
  // lTime is the cycle in which the latest use began
  Time_t lTime;

public:
  PortFullyPipe(const std::string &name);

  Time_t nextSlot(bool en);
  Time_t calcNextSlot() const;
};

class PortFullyNPipe : public PortGeneric {
private:
protected:
  const NumUnits_t nUnitsMinusOne;
  NumUnits_t       freeUnits;
  Time_t           lTime;

public:
  PortFullyNPipe(const std::string &name, NumUnits_t nFU);

  Time_t nextSlot(bool en);
  Time_t calcNextSlot() const;
};

class PortPipe : public PortGeneric {
private:
protected:
  const TimeDelta_t ocp;
  Time_t            lTime;

public:
  PortPipe(const std::string &name, TimeDelta_t occ);

  Time_t nextSlot(bool en);
  Time_t calcNextSlot() const;
};

class PortNPipe : public PortGeneric {
private:
protected:
  const TimeDelta_t   ocp;
  const NumUnits_t    nUnits;
  std::vector<Time_t> portBusyUntil;

public:
  PortNPipe(const std::string &name, NumUnits_t nFU, TimeDelta_t occ);
  virtual ~PortNPipe();

  Time_t nextSlot(bool en);
  Time_t nextSlot(int32_t occupancy, bool en);
  Time_t calcNextSlot() const;
};
