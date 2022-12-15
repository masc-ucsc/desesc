// See LICENSE.txt for details

#include <signal.h>
#include <sys/types.h>

#include "iassert.hpp"

#include "bootloader.hpp"

int main(int argc, const char **argv) {

  BootLoader::plug(argc, argv);
  BootLoader::boot();
  BootLoader::report("done");
  BootLoader::unboot();
  BootLoader::unplug();

  return 0;
}
