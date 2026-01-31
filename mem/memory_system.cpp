// See LICENSE for details.

#include "memory_system.hpp"

#include <cmath>
#include <cstdio>

#include "bus.hpp"
#include "ccache.hpp"
#include "config.hpp"
#include "drawarch.hpp"
#include "mem_controller.hpp"
#include "memxbar.hpp"
#include "nice_cache.hpp"
#include "unmemxbar.hpp"

extern DrawArch arch;

Memory_system::Memory_system(int32_t processorId) : Gmemory_system(processorId) { build_memory_system(); }

MemObj* Memory_system::buildMemoryObj(const std::string& device_type, const std::string& dev_section, const std::string& dev_name)
/* build the correct memory object {{{1 */
{
  // Returns new created MemoryObj or NULL in known-error mode
  MemObj*  mdev    = 0;
  uint32_t devtype = 0;  // CCache

  std::string mystr("");
  // You may insert here the further specializations you may need
  if (device_type == "cache") {
    mdev    = new CCache(this, dev_section, dev_name);
    devtype = 0;
  } else if (device_type == "nice") {
    mdev    = new Nice_cache(this, dev_section, dev_name);
    devtype = 1;
  } else if (device_type == "bus") {
    mdev    = new Bus(this, dev_section, dev_name);
    devtype = 2;
  } else if (device_type == "memxbar") {
    mdev    = new MemXBar(this, dev_section, dev_name);
    devtype = 4;
  } else if (device_type == "memcontroller") {
    mdev    = new MemController(this, dev_section, dev_name);
    devtype = 5;
  } else {
    Config::add_error(fmt::format("unknown memory type:{} from section:{}", device_type, dev_section));
    return nullptr;
  }

  mystr += "\n\"";
  mystr += mdev->getName();

  switch (devtype) {
    case 0:  // CCache
    case 1:  // NiceCache
      mystr += "\"[shape=record,sides=5,peripheries=2,color=darkseagreen,style=filled]";
      break;
    case 2:  // bus
      mystr += "\"[shape=record,sides=5,peripheries=1,color=lightpink,style=filled]";
      break;
    case 3:  // tlb
      mystr += "\"[shape=record,sides=5,peripheries=1,color=lavender,style=filled]";
      break;
    case 4:  // MemXBar
      mystr += "\"[shape=record,sides=5,peripheries=1,color=thistle,style=filled]";
      break;
    case 5:  // void
      mystr += "\"[shape=record,sides=5,peripheries=1,color=skyblue,style=filled]";
      break;
    default: mystr += "\"[shape=record,sides=5,peripheries=3,color=white,style=filled]"; break;
  }
  arch.addObj(mystr);
  I(mdev);
  return mdev;
}
/* }}} */
