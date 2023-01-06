// See LICENSE for details.

#pragma once

#include <limits.h>

#include <memory>
#include <vector>
#include <array>

#include "depwindow.hpp"
#include "estl.hpp"
#include "iassert.hpp"
#include "instruction.hpp"
#include "stats.hpp"

class Resource;
class GMemorySystem;

class Cluster {
private:
  std::shared_ptr<Resource> buildUnit(const std::string &clusterName, uint32_t pos, std::shared_ptr<GMemorySystem> ms, std::shared_ptr<Cluster> cluster, Opcode type, GProcessor *gproc);

protected:
  DepWindow window;

  const int32_t MaxWinSize;
  int32_t       windowSize;

  int32_t nready;

  Stats_avg  winNotUsed;
  Stats_cntr rdRegPool;
  Stats_cntr wrRegPool;


  int32_t nRegs;
  int32_t regPool;
  bool    lateAlloc;

  uint32_t cpuid;

  std::string name;

  static inline int cluster_id_counter=1;
  int cluster_id;

  struct UnitEntry {
    PortGeneric *gen;
    int32_t      num;
    int32_t      occ;
  };

  static inline std::map<std::string, UnitEntry>  unitMap;
  static inline std::map<std::string, std::shared_ptr<Resource>> resourceMap;
  static inline std::map<std::string, std::pair<
    std::shared_ptr<Cluster>
    ,std::array<std::shared_ptr<Resource>, iMAX>
    >>  clusterMap;

  void delEntry() {
    windowSize++;
    I(windowSize <= MaxWinSize);
  }
  void newEntry() {
    windowSize--;
    I(windowSize >= 0);
  }

  Cluster(const std::string &clusterName, uint32_t pos, uint32_t cpuid);

public:
  virtual ~Cluster();

  static void unplug() {
    unitMap.clear();
    resourceMap.clear();
    clusterMap.clear();
  }

  void select(Dinst *dinst);

  virtual void executing(Dinst *dinst)           = 0;
  virtual void executed(Dinst *dinst)            = 0;
  virtual bool retire(Dinst *dinst, bool replay) = 0;

  static std::pair<
     std::shared_ptr<Cluster>
    ,std::array<std::shared_ptr<Resource>, iMAX>
    >
    create(const std::string &clusterName, uint32_t pos, std::shared_ptr<GMemorySystem> ms, uint32_t cpuid, GProcessor *gproc);

  const std::string &getName() const { return name; }
  int get_id() const { return cluster_id; }

  StallCause canIssue(Dinst *dinst) const;
  void       add_inst(Dinst *dinst);

  int32_t getAvailSpace() const {
    if (regPool < windowSize) {
      return regPool;
    }
    return windowSize;
  }
  int32_t getNReady() const { return nready; }
};

class ExecutingCluster : public Cluster {
  // This is SCOORE style. The instruction is removed from the queue at dispatch time
public:
  virtual ~ExecutingCluster() {}

  ExecutingCluster(const std::string &clusterName, uint32_t pos, uint32_t _cpuid) : Cluster(clusterName, pos, _cpuid) {}

  void executing(Dinst *dinst);
  void executed(Dinst *dinst);
  bool retire(Dinst *dinst, bool replay);
};

class ExecutedCluster : public Cluster {
public:
  virtual ~ExecutedCluster() {}

  ExecutedCluster(const std::string &clusterName, uint32_t pos, uint32_t _cpuid) : Cluster(clusterName, pos, _cpuid) {}

  void executing(Dinst *dinst);
  void executed(Dinst *dinst);
  bool retire(Dinst *dinst, bool replay);
};

class RetiredCluster : public Cluster {
public:
  virtual ~RetiredCluster() {}
  RetiredCluster(const std::string &clusterName, uint32_t pos, uint32_t _cpuid) : Cluster(clusterName, pos, _cpuid) {}

  void executing(Dinst *dinst);
  void executed(Dinst *dinst);
  bool retire(Dinst *dinst, bool replay);
};
