// see LICENSE.txt for details

#if 0
#include "stats.hpp"
#include "config.hpp"

#include "PowerModel.h"

int main(int argc, const char **argv) {

  Config::init("desesc.toml");

  auto section = Config::get_string("soc","pwrmodel");
  fmt::print("power uses section {}\n", section);

  PowerModel p;
  p.plug(section);
  p.printStatus();
  p.testWrapper();

  return 0;
}

#endif
