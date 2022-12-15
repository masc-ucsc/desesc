// See LICENSE.txt for details

#pragma once

#include <sys/time.h>

//#include "power_model.hpp"
#include "iassert.hpp"
#include "opcode.hpp"

class BootLoader {
private:
  static timeval     stTime;
  //static std::unique_ptr<PowerModel> pwrmodel;
  //static bool        doPower;

  static void check();

protected:
  static void plugEmulInterfaces();
  static void plugSimuInterfaces();
  static void createEmulInterface(const char *section, Hartid_t fid = 0);
  static void createSimuInterface(const char *section, Hartid_t i);

public:
  static int64_t sample_count;

  static void plug(int argc, const char **argv);
  static void plugSocket(int64_t cpid, int64_t fwu, int64_t gw, uint64_t lwcnt);
  static void boot();
  static void report(const char *str);
  static void reportSample();
  static void unboot();
  static void unplug();

  // Dump statistics while the program is still running
  static void reportOnTheFly(const char *file = 0); // eka, to be removed.
  static void startReportOnTheFly();
  static void stopReportOnTheFly();
};

