// See LICENSE for details.

#include <cmath>
#include <cstdio>

#include "memory_system.hpp"
#include "bus.hpp"
#include "ccache.hpp"
#include "drawarch.hpp"
#include "mem_controller.hpp"
#include "memxbar.hpp"
#include "nicecache.hpp"
#include "config.hpp"
#include "unmemxbar.hpp"

extern DrawArch arch;

/* }}} */

MemorySystem::MemorySystem(int32_t processorId)
    /* constructor {{{1 */
    : GMemorySystem(processorId) {}
/* }}} */

MemObj *MemorySystem::buildMemoryObj(const std::string &device_type, const std::string &dev_section, const std::string &dev_name)
/* build the correct memory object {{{1 */
{
  // Returns new created MemoryObj or NULL in known-error mode
  MemObj  *mdev    = 0;
  uint32_t devtype = 0;  // CCache

  std::string mystr("");
  // You may insert here the further specializations you may need
  if (!strcasecmp(device_type, "cache") || !strcasecmp(device_type, "icache")) {
    mdev    = new CCache(this, dev_section, dev_name);
    devtype = 0;
  } else if (!strcasecmp(device_type, "nicecache")) {
    mdev    = new NICECache(this, dev_section, dev_name);
    devtype = 1;
  } else if (!strcasecmp(device_type, "bus")) {
    mdev    = new Bus(this, dev_section, dev_name);
    devtype = 2;
  } else if (!strcasecmp(device_type, "tlb")) {
    devtype = 3;
    mdev    = new TLB(this, dev_section, dev_name);
  } else if (!strcasecmp(device_type, "memxbar")) {
    mdev    = new MemXBar(this, dev_section, dev_name);
    devtype = 4;
  } else if (!strcasecmp(device_type, "memcontroller")) {
    mdev    = new MemController(this, dev_section, dev_name);
    devtype = 5;
  } else if (!strcasecmp(device_type, "void")) {
    return new DummyMemObj(dev_section, dev_name);
  } else {
    // Check the lower level because it may have it
    return GMemorySystem::buildMemoryObj(device_type, dev_section, dev_name);
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
