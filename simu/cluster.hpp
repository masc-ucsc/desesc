// See LICENSE for details.

#pragma once

#include <limits.h>

#include <array>
#include <memory>
#include <vector>

#include "depwindow.hpp"
#include "estl.hpp"
#include "gmemory_system.hpp"
#include "iassert.hpp"
#include "instruction.hpp"
#include "stats.hpp"

class Resource;

class Cluster {
private:
  std::shared_ptr<Resource> buildUnit(const std::string &clusterName, uint32_t pos, std::shared_ptr<Gmemory_system> ms,
                                      std::shared_ptr<Cluster> cluster, Opcode type, GProcessor *gproc);

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

  static inline int cluster_id_counter = 1;
  int               cluster_id;

  struct UnitEntry {
    PortGeneric *gen;
    int32_t      num;
    int32_t      occ;
  };

  static inline std::map<std::string, UnitEntry>                                                                    unitMap;
  static inline std::map<std::string, std::shared_ptr<Resource>>                                                    resourceMap;
  static inline std::map<std::string, std::pair<std::shared_ptr<Cluster>, Opcode_array<std::shared_ptr<Resource>>>> clusterMap;

  /*void delEntry() {
    windowSize++;
    I(windowSize <= MaxWinSize);
  }*/
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

  void delEntry() {
    windowSize++;
    I(windowSize <= MaxWinSize);
  }
  void add_reg_pool() {
    regPool++;
    //I(regPool <= nRegs);
  }

//<<<<<<< HEAD
  void add_inst_retry( Dinst *dinst) {
    if(!dinst->is_in_cluster()) {
      window.add_inst(dinst);
    }
  }

  int32_t get_reg_pool() {
   return  regPool;
  }
//=======
  //int32_t get_reg_pool() { return regPool; }

  int32_t get_nregs() { return nRegs; }

  int32_t get_window_size() { return windowSize; }

  int32_t get_window_maxsize() { return MaxWinSize; }
//>>>>>>> upstream/main

  void select(Dinst *dinst);

  virtual void executing(Dinst *dinst)           = 0;
  virtual void executed(Dinst *dinst)            = 0;
  virtual bool retire(Dinst *dinst, bool replay) = 0;
  virtual void flushed(Dinst *dinst)             = 0;

  static std::pair<std::shared_ptr<Cluster>, Opcode_array<std::shared_ptr<Resource>>> create(const std::string &clusterName,
                                                                                             uint32_t           pos,
                                                                                             std::shared_ptr<Gmemory_system> ms,
                                                                                             uint32_t cpuid, GProcessor *gproc);

  const std::string &getName() const { return name; }
  int                get_id() const { return cluster_id; }

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
  void flushed(Dinst *dinst);
};

class ExecutedCluster : public Cluster {
public:
  virtual ~ExecutedCluster() {}

  ExecutedCluster(const std::string &clusterName, uint32_t pos, uint32_t _cpuid) : Cluster(clusterName, pos, _cpuid) {}

  void executing(Dinst *dinst);
  void executed(Dinst *dinst);
  bool retire(Dinst *dinst, bool replay);
  void flushed(Dinst *dinst);
};

class RetiredCluster : public Cluster {
public:
  virtual ~RetiredCluster() {}
  RetiredCluster(const std::string &clusterName, uint32_t pos, uint32_t _cpuid) : Cluster(clusterName, pos, _cpuid) {}

  void executing(Dinst *dinst);
  void executed(Dinst *dinst);
  bool retire(Dinst *dinst, bool replay);
  void flushed(Dinst *dinst);
};
