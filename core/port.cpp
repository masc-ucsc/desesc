// See LICENSE for details.

#include "port.hpp"

PortGeneric::PortGeneric(const std::string &name) : avgTime(name) { nUsers = 0; }

PortGeneric::~PortGeneric() {
  if (nUsers) {
    fmt::print("ERROR: Not enough destroys for FUGenUnit\n");
  }
}

PortGeneric *PortGeneric::create(const std::string &unitName, NumUnits_t nUnits, TimeDelta_t occ) {
  // Search for the configuration with the best performance. In theory
  // everything can be solved with PortNPipe, but it is the slowest.
  // Sorry, but I'm a performance freak!

  auto name = fmt::format("{}_occ", unitName);

  PortGeneric *gen;

  if (occ == 0 || nUnits == 0) {
    gen = new PortUnlimited(name);
  } else if (occ == 1) {
    if (nUnits == 1) {
      gen = new PortFullyPipe(name);
    } else {
      gen = new PortFullyNPipe(name, nUnits);
    }
  } else {
    if (nUnits == 1) {
      gen = new PortPipe(name, occ);
    } else {
      gen = new PortNPipe(name, nUnits, occ);
    }
  }

  return gen;
}

void PortGeneric::destroy() { delete this; }

void PortGeneric::occupyUntil(Time_t u) {
  Time_t t = globalClock;

  while (t < u) {
    t = nextSlot(false);
  }
}

PortUnlimited::PortUnlimited(const std::string &name) : PortGeneric(name) {}

Time_t PortUnlimited::nextSlot(bool en) {
  avgTime.sample(0, en);  // Just to keep usage statistics
  return globalClock;
}

void PortUnlimited::occupyUntil(Time_t u) { (void)u; }

Time_t PortUnlimited::calcNextSlot() const { return globalClock; }

PortFullyPipe::PortFullyPipe(const std::string &name) : PortGeneric(name) { lTime = globalClock; }

Time_t PortFullyPipe::nextSlot(bool en) {
#ifndef NDEBUG
  Time_t cns = calcNextSlot();
#endif

  if (lTime < globalClock) {
    lTime = globalClock;
  }

#ifndef NDEBUG
  I(cns == lTime);
#endif
  avgTime.sample(lTime - globalClock, en);
  return lTime++;
}

Time_t PortFullyPipe::calcNextSlot() const { return ((lTime < globalClock) ? globalClock : lTime); }

PortFullyNPipe::PortFullyNPipe(const std::string &name, NumUnits_t nFU) : PortGeneric(name), nUnitsMinusOne(nFU - 1) {
  I(nFU > 0);  // For unlimited resources use the FUUnlimited

  lTime = globalClock;

  freeUnits = nFU;
}

Time_t PortFullyNPipe::nextSlot(bool en) {
#ifndef NDEBUG
  Time_t cns = calcNextSlot();
#endif

  if (lTime < globalClock) {
    lTime     = globalClock;
    freeUnits = nUnitsMinusOne;
  } else if (freeUnits > 0) {
    freeUnits--;
  } else {
    lTime++;
    freeUnits = nUnitsMinusOne;
  }

#ifndef NDEBUG
  I(cns == lTime);
#endif
  avgTime.sample(lTime - globalClock, en);
  return lTime;
}

Time_t PortFullyNPipe::calcNextSlot() const { return ((lTime < globalClock) ? globalClock : (lTime + (freeUnits == 0))); }

PortPipe::PortPipe(const std::string &name, TimeDelta_t occ) : PortGeneric(name), ocp(occ) {
  lTime = (globalClock > ocp) ? globalClock - ocp : 0;
}

Time_t PortPipe::nextSlot(bool en) {
#ifndef NDEBUG
  Time_t cns = calcNextSlot();
#endif

  if (lTime < globalClock) {
    lTime = globalClock;
  }

  Time_t st = lTime;
  lTime += ocp;

#ifndef NDEBUG
  I(cns == st);
#endif
  avgTime.sample(st - globalClock, en);
  return st;
}

Time_t PortPipe::calcNextSlot() const { return ((lTime < globalClock) ? globalClock : lTime); }

PortNPipe::PortNPipe(const std::string &name, NumUnits_t nFU, TimeDelta_t occ) : PortGeneric(name), ocp(occ), nUnits(nFU) {
  portBusyUntil.resize(nFU, globalClock);
}

PortNPipe::~PortNPipe() {}

Time_t PortNPipe::nextSlot(bool en) { return nextSlot(ocp, en); }

Time_t PortNPipe::nextSlot(int32_t occupancy, bool en) {
#ifndef NDEBUG
  Time_t cns = calcNextSlot();
#endif
  Time_t bufTime = portBusyUntil[0];

  if (bufTime < globalClock) {
    portBusyUntil[0] = globalClock + occupancy;
#ifndef NDEBUG
    I(cns == globalClock);
#endif
    avgTime.sample(0, en);
    return globalClock;
  }

  NumUnits_t bufPort = 0;

  for (NumUnits_t i = 1; i < nUnits; i++) {
    if (portBusyUntil[i] < globalClock) {
      portBusyUntil[i] = globalClock + occupancy;
#ifndef NDEBUG
      I(cns == globalClock);
#endif
      avgTime.sample(0, en);
      return globalClock;
    }
    if (portBusyUntil[i] < bufTime) {
      bufPort = i;
      bufTime = portBusyUntil[i];
    }
  }

  portBusyUntil[bufPort] += occupancy;

#ifndef NDEBUG
  I(cns == bufTime);
#endif
  avgTime.sample(bufTime - globalClock, en);
  return bufTime;
}

Time_t PortNPipe::calcNextSlot() const {
  Time_t firsttime = portBusyUntil[0];

  for (NumUnits_t i = 1; i < nUnits; i++) {
    if (portBusyUntil[i] < firsttime) {
      firsttime = portBusyUntil[i];
    }
  }
  return (firsttime < globalClock) ? globalClock : firsttime;
}
