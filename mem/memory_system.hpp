// See LICENSE for details.

#pragma once

#include "gmemorysystem.hpp"
#include "iassert.hpp"
#include "memobj.hpp"

class MemorySystem : public GMemorySystem {
private:
protected:
  MemObj *buildMemoryObj(const std::string &type, const std::string &section, const std::string &name) override;

public:
  MemorySystem(int32_t processorId);
};

