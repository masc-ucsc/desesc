// See LICENSE for details.

#include "GMemorySystem.h"

#include <cstdlib>
#include <string>

#include "DrawArch.h"
#include "MemObj.h"
#include "absl/strings/str_split.h"
#include "config.hpp"

MemoryObjContainer            GMemorySystem::sharedMemoryObjContainer;
GMemorySystem::StrCounterType GMemorySystem::usedNames;
std::vector<std::string>      GMemorySystem::MemObjNames;
DrawArch                      arch;

//////////////////////////////////////////////
// MemoryObjContainer

void MemoryObjContainer::addMemoryObj(const std::string &device_name, MemObj *obj) {
  intlMemoryObjContainer[device_name] = obj;
  mem_node.push_back(obj);
}

MemObj *MemoryObjContainer::searchMemoryObj(const std::string &descr_section, const std::string &device_name) const {
  I(!descr_section.empty());
  I(!device_name.empty());

  StrToMemoryObjMapper::const_iterator it = intlMemoryObjContainer.find(device_name);

  if (it == intlMemoryObjContainer.end()) {
    return nullptr;
  }

  const auto sec = it->second->getSection();
  if (sec == descr_section) {
    Config::add_error(
        fmt::format("Two versions of MemoryObject [{}] with different definitions [{}] and [{}]", device_name, sec, descr_section));
    return nullptr;
  }

  return it->second;
}

/* Only returns a pointer if there is only one with that name */
MemObj *MemoryObjContainer::searchMemoryObj(const std::string &device_name) const {
  I(!device_name.empty());

  if (intlMemoryObjContainer.count(device_name) != 1) {
    return nullptr;
  }

  return intlMemoryObjContainer.find(device_name)->second;
}

void MemoryObjContainer::clear() { intlMemoryObjContainer.clear(); }

//////////////////////////////////////////////
// GMemorySystem

GMemorySystem::GMemorySystem(int32_t processorId) : coreId(processorId) {
  localMemoryObjContainer = new MemoryObjContainer();

  DL1  = 0;
  IL1  = 0;
  vpc  = 0;
  pref = 0;

  priv_counter = 0;
}

GMemorySystem::~GMemorySystem() {
  if (DL1) {
    delete DL1;
  }

  if (IL1 && DL1 != IL1) {
    delete IL1;
  }

  if (vpc) {
    delete vpc;
  }

  if (pref) {
    delete pref;
  }

  delete localMemoryObjContainer;
}

MemObj *GMemorySystem::buildMemoryObj(const std::string &type, const std::string &section, const std::string &name) {
  if (!(type == "dummy" || type == "cache" || type == "icache" || type == "scache"

        || type == "markovPrefetcher" || type == "stridePrefetcher" || type == "Prefetcher"

        || type == "splitter" || type == "siftsplitter"

        || type == "smpcache" || type == "memxbar")) {
    Config::add_error(fmt::format("Invalid memory type [{}]", type));
  }

  return new DummyMemObj(section, name);
}

void GMemorySystem::buildMemorySystem() {
  std::string def_block = Config::get_string("soc", "core", coreId);

  IL1 = declareMemoryObj(def_block, "il1");
  IL1->getRouter()->fillRouteTables();
  IL1->setCoreIL1(coreId);

  if (IL1->get_type() == "tlb") {
    IL1->getRouter()->getDownNode()->setCoreIL1(coreId);
  }

  DL1 = declareMemoryObj(def_block, "dl1");
  DL1->getRouter()->fillRouteTables();
  DL1->setCoreDL1(coreId);

  if (DL1->get_type() == "tlb") {
    DL1->getRouter()->getDownNode()->setCoreDL1(coreId);
  } else if (DL1->get_type() == "prefetcher") {
    DL1->getRouter()->getDownNode()->setCoreDL1(coreId);
  }
}

std::string GMemorySystem::buildUniqueName(const std::string &device_type) {
  int32_t num;

  auto it = usedNames.find(device_type);
  if (it == usedNames.end()) {
    usedNames[device_type] = 0;
    num                    = 0;
  } else {
    num = ++(*it).second;
  }

  return fmt::format("{}({})", device_type, num);
}

std::string GMemorySystem::privatizeDeviceName(const std::string &given_name, int32_t num) {
  return fmt::format("{}({})", given_name, num);
}

MemObj *GMemorySystem::searchMemoryObj(bool shared, const std::string &section, const std::string &name) const {
  return getMemoryObjContainer(shared)->searchMemoryObj(section, name);
}

MemObj *GMemorySystem::searchMemoryObj(bool shared, const std::string &name) const {
  return getMemoryObjContainer(shared)->searchMemoryObj(name);
}

MemObj *GMemorySystem::declareMemoryObj_uniqueName(const std::string &name, const std::string &device_descr_section) {
#if 0
  std::vector<std::string> vPars;
  vPars.push_back(device_descr_section);
  vPars.push_back(name);
  vPars.push_back("shared");
#endif

  return finishDeclareMemoryObj({device_descr_section, name, "shared"});
}

MemObj *GMemorySystem::declareMemoryObj(const std::string &block, const std::string &field) {
  auto                     str   = Config::get_string(block, field);
  std::vector<std::string> vPars = absl::StrSplit(str, ' ');

  if (vPars.empty()) {
    Config::add_error(fmt::format("section [{}] field [{}] does not describe a MemoryObj", block, field));
    Config::add_error("required format: memoryDevice = descriptionSection [name] [shared|private]");
    return nullptr;
  }

  MemObj *ret = finishDeclareMemoryObj(vPars);
  I(ret);  // Users of declareMemoryObj dereference without nullptr check so pointer must be valid
  return ret;
}

MemObj *GMemorySystem::finishDeclareMemoryObj(const std::vector<std::string> &vPars, const std::string &name_suffix) {
  bool shared     = false;  // Private by default
  bool privatized = false;

  std::string device_name = (vPars.size() > 1) ? vPars[1] : "";
  std::string shared_arg  = (vPars.size() > 2) ? vPars[2] : "";

  if (!shared_arg.empty()) {
    std::transform(shared_arg.begin(), shared_arg.end(), shared_arg.begin(), [](unsigned char c) { return std::tolower(c); });

    if (shared_arg == "shared") {
      I(vPars.size() == 3);
      shared = true;
    } else if (shared_arg == "sharedby") {
      I(vPars.size() == 4);
      int32_t sharedBy = std::stoi(vPars[3]);
      // delete[] vPars[3];
      if (sharedBy < 0) {
        Config::add_error(fmt::format("sharedby should be bigger than zero (field {})", device_name));
        return nullptr;
      }

      int32_t nId = coreId / sharedBy;
      device_name = privatizeDeviceName(device_name, nId);
      shared      = true;
      privatized  = true;
    }
  } else if (!device_name.empty()) {
    if (device_name == "shared") {
      shared = true;
    }
  }

  std::string device_descr_section = vPars[0];
  std::string device_type          = Config::get_string(device_descr_section, "type");

  /* If the device has been given a name, we may be refering to an
   * already existing device in the system, so let's search
   * it. Anonymous devices (no name given) are always unique, and only
   * one reference to them may exist in the system.
   */

  if (!device_name.empty()) {
    if (!privatized) {
      if (shared) {
        device_name = privatizeDeviceName(device_name, 0);
      } else {
        device_name = privatizeDeviceName(device_name, coreId);
      }
    }
    device_name = fmt::format("{}{}", device_name, name_suffix);

    MemObj *memdev = searchMemoryObj(shared, device_descr_section, device_name);
    if (memdev) {
      return memdev;
    }

  } else {
    device_name = buildUniqueName(device_type);
  }

  MemObj *newMem = buildMemoryObj(device_type, device_descr_section, device_name);
  if (newMem) {  // Would be 0 in known-error mode
    getMemoryObjContainer(shared)->addMemoryObj(device_name, newMem);
  }

  return newMem;
}

DummyMemorySystem::DummyMemorySystem(int32_t _coreId) : GMemorySystem(_coreId) {
  // Do nothing
}

DummyMemorySystem::~DummyMemorySystem() {
  // Do nothing
}
