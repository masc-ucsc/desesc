// See LICENSE for details.

#include "simu_base.hpp"

#include "config.hpp"
#include "fmt/format.h"

Simu_base::Simu_base(std::shared_ptr<Gmemory_system> gm, Hartid_t hid_)
    : power_down(false)
    , nFreeze(fmt::format("({}):nFreeze", hid_))
    , clockTicks(fmt::format("({}):clockTicks", hid_))
    , hid(hid_)
    , memorySystem(gm) {
  power_down = false;

  lastUpdatedWallClock = lastWallClock;
  activeclock_start    = lastWallClock;
  activeclock_end      = lastWallClock;

  auto   sz      = Config::get_array_size("soc", "core");
  double max_mhz = 0;
  for (auto i = 0u; i < sz; ++i) {
    auto mhz = Config::get_integer("soc", "core", i, "frequency_mhz", 1, 32000);  // 32GHz!!!
    if (mhz > max_mhz) {
      max_mhz = mhz;
    }
  }

  frequency_mhz = Config::get_integer("soc", "core", hid, "frequency_mhz");
  clock_ratio   = max_mhz / frequency_mhz;
  clock_counter = 0;
  I(clock_ratio <= 1);

  fmt::print("core:{} freq:{} raio:{}\n", hid, frequency_mhz, clock_ratio);

  eint = nullptr;
}

bool Simu_base::adjust_clock(bool en) {
  clockTicks.inc(en);

  if (activeclock_end != (lastWallClock - 1)) {
    activeclock_start = lastWallClock;
  }
  activeclock_end = lastWallClock;

  if (lastWallClock != globalClock && en) {
    lastWallClock = globalClock;
    wallclock.inc(true);
  }

  if (clock_ratio >= 1) {
    return true;
  }

  clock_counter += clock_ratio;
  if (clock_counter < 1) {
    return false;
  }

  clock_counter -= 1;

  return true;
}
