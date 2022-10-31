// See LICENSE for details.

#pragma once

#include <strings.h>
#include <string>
#include <vector>

#include "iassert.hpp"

#include "estl.h"

class MemObj;

// Class for comparison to be used in hashes of char * where the
// content is to be compared
class MemObjCaseeqstr {
public:
  inline bool operator()(const char *s1, const char *s2) const {
    return strcasecmp(s1, s2) == 0;
  }
};

class MemoryObjContainer {
private:
  std::vector<MemObj *>                   mem_node;
  typedef std::map<std::string, MemObj *> StrToMemoryObjMapper;
  // typedef HASH_MAP<const char *, MemObj *, HASH<const char*>, MemObjCaseeqstr> StrToMemoryObjMapper;
  StrToMemoryObjMapper intlMemoryObjContainer;

public:
  void addMemoryObj(const char *device_name, MemObj *obj);

  MemObj *searchMemoryObj(const char *section, const char *name) const;
  MemObj *searchMemoryObj(const char *name) const;

  void clear();
};

class GMemorySystem {
private:
  typedef std::map<std::string, uint32_t> StrCounterType;
  // typedef HASH_MAP<const char*, uint32_t, HASH<const char*>, MemObjCaseeqstr > StrCounterType;
  static StrCounterType usedNames;

  static MemoryObjContainer sharedMemoryObjContainer;
  MemoryObjContainer *      localMemoryObjContainer;

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

  MemObj *DL1;  // Data L1 cache
  MemObj *IL1;  // Instruction L1 cache
  MemObj *pref; // Prefetcher
  MemObj *vpc;  // Speculative virtual predictor cache

protected:
  const uint32_t coreId;

  char *buildUniqueName(const char *device_type);

  uint32_t priv_counter;

  static char *privatizeDeviceName(char *given_name, int32_t num);

  virtual MemObj *buildMemoryObj(const char *type, const char *section, const char *name);

public:
  GMemorySystem(int32_t processorId);
  virtual ~GMemorySystem();

  // The code can not be located in constructor because it is nor possible to
  // operate with virtual functions at construction time
  virtual void buildMemorySystem();

  MemObj *searchMemoryObj(bool shared, const char *section, const char *name) const;
  MemObj *searchMemoryObj(bool shared, const char *name) const;

  MemObj *declareMemoryObj_uniqueName(char *name, char *device_descr_section);
  MemObj *declareMemoryObj(const char *block, const char *field);
  MemObj *finishDeclareMemoryObj(std::vector<char *> vPars, char *name_suffix = NULL);

  uint32_t getNumMemObjs() {
    return MemObjNames.size();
  }
  void addMemObjName(const char *name) {
    MemObjNames.push_back(name);
  }
  std::string getMemObjName(uint32_t i) {
    return MemObjNames[i];
  }

  uint32_t getCoreId() const {
    return coreId;
  };
  MemObj *getDL1() const {
    return DL1;
  };
  MemObj *getIL1() const {
    return IL1;
  };
  MemObj *getvpc() const {
    return vpc;
  };
  MemObj *getPrefetcher() const {
    return pref;
  };
};

class DummyMemorySystem : public GMemorySystem {
private:
protected:
public:
  DummyMemorySystem(int32_t coreId);
  ~DummyMemorySystem();
};

