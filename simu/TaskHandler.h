// See LICENSE for details.

#pragma once

//#define ENABLE_MP 0
#include <pthread.h>

#include <vector>

#include "emul_base.hpp"
#include "iassert.hpp"

class GProcessor;

class TaskHandler {
private:
  class EmulSimuMapping {
  public:
    FlowID         fid;
    bool           active;
    bool           deactivating;
    EmulInterface *emul;
    GProcessor    *simu;
  };

  typedef std::vector<EmulSimuMapping> AllMapsType;
  typedef FlowID                      *runningType;

  static AllMapsType   allmaps;
  static volatile bool terminate_all;

  static runningType     running;
  static FlowID          running_size;
  static pthread_mutex_t mutex;
  static pthread_mutex_t mutex_terminate;

  static std::vector<EmulInterface *> emulas;  // associated emula
  static std::vector<GProcessor *>    cpus;    // All the CPUs in the system

  static void removeFromRunning(FlowID fid);

public:
  static void freeze(FlowID fid, Time_t nCycles);

  static FlowID resumeThread(FlowID uid, FlowID last_fid);
  static FlowID resumeThread(FlowID uid);
  static void   pauseThread(FlowID fid);
  static void   terminate();

  static void report(const char *str);

  static void addEmul(EmulInterface *eint, FlowID fid = 0);
  static void addEmulShared(EmulInterface *eint);
  static void addSimu(GProcessor *gproc);

  static bool isTerminated() { return terminate_all; }

  static bool isActive(FlowID fid) {
    I(fid < allmaps.size());
    return allmaps[fid].active;
  }

  static FlowID getNumActiveCores();
  static FlowID getNumCores() { return allmaps.size(); }

  static FlowID getNumCPUS() {
    I(cpus.size() > 0);
    return cpus.size();
  }

  static EmulInterface *getEmul(FlowID fid) {
    I(fid < emulas.size());
    return emulas[fid];
  };

  static GProcessor *getSimu(FlowID fid) {
    I(fid < cpus.size());
    return cpus[fid];
  };

  static void plugBegin();
  static void plugEnd();
  static void boot();
  static void unboot();
  static void unplug();

  static void syncStats();
};
