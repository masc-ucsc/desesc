// See LICENSE.txt for details

#include <signal.h>
#include <sys/types.h>

#include "bootloader.hpp"
#include "iassert.hpp"

int main(int argc, const char **argv) {
  BootLoader::plug(argc, argv);
  BootLoader::boot();
  BootLoader::report("done");
  BootLoader::unboot();
  BootLoader::unplug();

  return 0;
}
