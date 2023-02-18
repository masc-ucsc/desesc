// See LICENSE for details.

#pragma once

#include <string>

#include "gmemory_system.hpp"
#include "iassert.hpp"
#include "memobj.hpp"

class Memory_system : public Gmemory_system {
private:
protected:
  MemObj *buildMemoryObj(const std::string &type, const std::string &section, const std::string &name) override;

public:
  Memory_system(int32_t processorId);
};
