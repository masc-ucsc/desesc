// See LICENSE.txt for details

#pragma once

#include <sys/time.h>

#include <string>

// #include "power_model.hpp"
#include "iassert.hpp"
#include "opcode.hpp"

class BootLoader {
private:
  static timeval stTime;
  // static std::unique_ptr<PowerModel> pwrmodel;
  // static bool        doPower;

  static void check();

protected:
  static void plug_emuls();
  static void plug_simus();

public:
  static int64_t sample_count;

  static void plug(int argc, const char **argv);
  static void boot();
  static void report(const std::string &str);
  static void unboot();
  static void unplug();

  // Dump statistics while the program is still running
  static void reportOnTheFly();  // eka, to be removed.
  static void startReportOnTheFly();
  static void stopReportOnTheFly();
};
