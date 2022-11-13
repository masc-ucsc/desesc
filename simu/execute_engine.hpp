// See LICENSE for details.

#pragma once

#include <string>

#include "GMemorySystem.h"
#include "emul_base.hpp"
#include "snippets.hpp"
#include "stats.hpp"

class Execute_engine {
private:
  static inline Time_t lastWallClock{0};

  uint64_t frequency_mhz;
  Time_t   lastUpdatedWallClock;
  Time_t   activeclock_start;
  Time_t   activeclock_end;

  static inline Stats_cntr wallclock{"OS:wallclock"};

  bool power_down;

  Stats_cntr nFreeze;
  Stats_cntr clockTicks;

  float clock_ratio;
  float clock_counter;

protected:
  const Hartid_t hid;

  std::shared_ptr<Emul_base> eint;
  GMemorySystem             *memorySystem;

  bool adjust_clock();

  Execute_engine(GMemorySystem *gm, Hartid_t i);

public:
  void set_emul(std::shared_ptr<Emul_base> e) { eint = e; }

  void freeze(Time_t nCycles) {
    nFreeze.add(nCycles);
    clockTicks.add(nCycles);
  }

  Hartid_t get_hid() const { return hid; }

  void set_power_up() { power_down = false; }
  void set_power_down() { power_down = true; }
  bool is_power_up() const { return !power_down; }
  bool is_power_down() const { return power_down; }

  void adjust_clock(bool en = true) {
    clockTicks.inc(en);

    trackactivity();

    if (lastWallClock == globalClock || !en)
      return;

    lastWallClock = globalClock;
    wallclock.inc(en);
  }
  static Time_t getWallClock() { return lastWallClock; }

  void trackactivity() {
    if (activeclock_end != (lastWallClock - 1)) {
      activeclock_start = lastWallClock;
    }
    activeclock_end = lastWallClock;
  }

  GMemorySystem *ref_memory_system() const { return memorySystem; }

  // API for Execute_engine
  virtual bool        advance_clock_drain() = 0;
  virtual bool        advance_clock()       = 0;
  virtual std::string get_type() const      = 0;
};
