// See LICENSE for details.

#pragma once

#include <vector>

#include "absl/container/flat_hash_set.h"
#include "emul_base.hpp"
#include "iassert.hpp"

class GProcessor;

class TaskHandler {
private:
  class EmulSimuMapping {
  public:
    Hartid_t                    fid;
    bool                        active;
    bool                        deactivating;
    std::shared_ptr<Emul_base>  emul;
    std::shared_ptr<GProcessor> simu;
  };

  static inline bool terminate_all{false};

  static inline std::vector<EmulSimuMapping> allmaps;

  static inline absl::flat_hash_set<Hartid_t> running;

  static inline std::vector<std::shared_ptr<Emul_base> >  emuls;  // associated emula
  static inline std::vector<std::shared_ptr<GProcessor> > simus;  // All the simus in the system

  static inline bool plugging{false};

public:
  static void core_create(std::shared_ptr<GProcessor> gproc);
  static void core_resume(Hartid_t uid);
  static void core_pause(Hartid_t fid);
  static void core_freeze(Hartid_t fid, Time_t nCycles);
  static void core_terminate_all();

  static bool is_core_power_up(Hartid_t fid) {
    I(fid < allmaps.size());
    return allmaps[fid].active;
  }

  static void report();

  static void add_emul(std::shared_ptr<Emul_base> eint, Hartid_t hid);

  static Hartid_t getNumActiveCores();
  static Hartid_t getNumCores() { return allmaps.size(); }

  static Hartid_t getNumCPUS() {
    I(simus.size() > 0);
    return simus.size();
  }

  static std::shared_ptr<Emul_base> ref_emul(Hartid_t fid) {
    I(fid < emuls.size());
    return emuls[fid];
  };

  static std::shared_ptr<GProcessor> ref_core(Hartid_t fid) {
    I(fid < simus.size());
    return simus[fid];
  };

  static void plugBegin();
  static void plugEnd();
  static void boot();
  static void unboot();
  static void unplug();

  static void syncStats();
};
