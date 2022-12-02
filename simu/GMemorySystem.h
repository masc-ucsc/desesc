// See LICENSE for details.

#pragma once

#include <strings.h>

#include <string>
#include <vector>

#include "estl.h"
#include "iassert.hpp"

class MemObj;

class MemoryObjContainer {
private:
  std::vector<MemObj *>                   mem_node;
  typedef std::map<std::string, MemObj *> StrToMemoryObjMapper;
  StrToMemoryObjMapper                    intlMemoryObjContainer;

public:
  void addMemoryObj(const std::string &device_name, MemObj *obj);

  MemObj *searchMemoryObj(const std::string &section, const std::string &name) const;
  MemObj *searchMemoryObj(const std::string &name) const;

  void clear();
};

class GMemorySystem {
private:
  typedef std::map<std::string, uint32_t> StrCounterType;
  static StrCounterType                   usedNames;

  static MemoryObjContainer sharedMemoryObjContainer;
  MemoryObjContainer       *localMemoryObjContainer;

  const MemoryObjContainer *getMemoryObjContainer(bool shared) const {
    MemoryObjContainer *mo = shared ? &sharedMemoryObjContainer : localMemoryObjContainer;
    I(mo);
    return mo;
  }

  MemoryObjContainer *getMemoryObjContainer(bool shared) {
    MemoryObjContainer *mo = shared ? &sharedMemoryObjContainer : localMemoryObjContainer;
    I(mo);
    return mo;
  }

  static std::vector<std::string> MemObjNames;

  MemObj *DL1;   // Data L1 cache
  MemObj *IL1;   // Instruction L1 cache
  MemObj *pref;  // Prefetcher
  MemObj *vpc;   // Speculative virtual predictor cache

protected:
  const uint32_t coreId;

  std::string buildUniqueName(const std::string &device_type);

  uint32_t priv_counter;

  static std::string privatizeDeviceName(const std::string &given_name, int32_t num);

  virtual MemObj *buildMemoryObj(const std::string &type, const std::string &section, const std::string &name);

public:
  GMemorySystem(int32_t processorId);
  virtual ~GMemorySystem();

  // The code can not be located in constructor because it is nor possible to
  // operate with virtual functions at construction time
  virtual void buildMemorySystem();

  MemObj *searchMemoryObj(bool shared, const std::string &section, const std::string &name) const;
  MemObj *searchMemoryObj(bool shared, const std::string &name) const;

  MemObj *declareMemoryObj_uniqueName(const std::string &name, const std::string &device_descr_section);
  MemObj *declareMemoryObj(const std::string &block, const std::string &field);
  MemObj *finishDeclareMemoryObj(const std::vector<std::string> &vPars, const std::string &name_suffix = "");

  uint32_t    getNumMemObjs() { return MemObjNames.size(); }
  void        addMemObjName(const std::string &name) { MemObjNames.push_back(name); }
  std::string getMemObjName(uint32_t i) { return MemObjNames[i]; }

  uint32_t getCoreId() const { return coreId; };
  MemObj  *getDL1() const { return DL1; };
  MemObj  *getIL1() const { return IL1; };
  MemObj  *getvpc() const { return vpc; };
  MemObj  *getPrefetcher() const { return pref; };
};

class DummyMemorySystem : public GMemorySystem {
private:
protected:
public:
  DummyMemorySystem(int32_t coreId);
  ~DummyMemorySystem();
};
