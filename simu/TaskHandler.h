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
    Hartid_t         fid;
    bool           active;
    bool           deactivating;
    EmulInterface *emul;
    GProcessor    *simu;
  };

  typedef std::vector<EmulSimuMapping> AllMapsType;
  typedef Hartid_t                      *runningType;

  static AllMapsType   allmaps;
  static volatile bool terminate_all;

  static runningType     running;
  static Hartid_t          running_size;
  static pthread_mutex_t mutex;
  static pthread_mutex_t mutex_terminate;

  static std::vector<EmulInterface *> emulas;  // associated emula
  static std::vector<GProcessor *>    cpus;    // All the CPUs in the system

  static void removeFromRunning(Hartid_t fid);

public:
  static void freeze(Hartid_t fid, Time_t nCycles);

  static Hartid_t resumeThread(Hartid_t uid, Hartid_t last_fid);
  static Hartid_t resumeThread(Hartid_t uid);
  static void   pauseThread(Hartid_t fid);
  static void   terminate();

  static void report(const char *str);

  static void addEmul(EmulInterface *eint, Hartid_t fid = 0);
  static void addEmulShared(EmulInterface *eint);
  static void addSimu(GProcessor *gproc);

  static bool isTerminated() { return terminate_all; }

  static bool isActive(Hartid_t fid) {
    I(fid < allmaps.size());
    return allmaps[fid].active;
  }

  static Hartid_t getNumActiveCores();
  static Hartid_t getNumCores() { return allmaps.size(); }

  static Hartid_t getNumCPUS() {
    I(cpus.size() > 0);
    return cpus.size();
  }

  static EmulInterface *getEmul(Hartid_t fid) {
    I(fid < emulas.size());
    return emulas[fid];
  };

  static GProcessor *getSimu(Hartid_t fid) {
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
