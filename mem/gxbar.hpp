// See LICENSE for details.

#pragma once

#include "stats.hpp"
#include "memobj.hpp"
#include "memrequest.hpp"
#include "memory_system.hpp"
#include "port.hpp"

class GXBar : public MemObj {

protected:
  enum BypassMode { bypass_none, bypass_global, bypass_shared } bypassMode;

  uint32_t common_addr_hash(Addr_t addr, uint32_t LineSize, uint32_t numLowerBanks) const {
    uint32_t numLineBits = log2i(LineSize);

    addr = addr >> numLineBits;

    return (addr & (numLowerBanks - 1));
  }

  static inline uint32_t Xbar_unXbar_balance = 0;

public:
  GXBar(const std::string &device_descr_section, const std::string &device_name = NULL);
  ~GXBar() {
  }
};

