// See LICENSE for details.

#include "bootloader.hpp"

#include <pthread.h>
#include <signal.h>
#include <stdlib.h>

#include "accprocessor.hpp"
#include "config.hpp"
#include "drawarch.hpp"
#include "emul_dromajo.hpp"
#include "gmemory_system.hpp"
#include "gprocessor.hpp"
#include "gpusmprocessor.hpp"
#include "inorderprocessor.hpp"
#include "memory_system.hpp"
#include "oooprocessor.hpp"
#include "report.hpp"
#include "taskhandler.hpp"

extern DrawArch arch;

extern "C" void signalCatcher(int32_t sig);

extern "C" void signalCatcherUSR1(int32_t sig) {
  fmt::print("WARNING: signal {} received. Dumping partial statistics\n", sig);

  BootLoader::reportOnTheFly();

  signal(SIGUSR1, signalCatcherUSR1);
}

extern "C" void signalCatcher(int32_t sig) {
  fmt::print("Stopping simulation early!!\n");

  static bool sigFaulting = false;
  if (sigFaulting) {
    TaskHandler::unplug();
    fmt::print("WARNING. Not a nice stop. It may leave pids\n");
    kill(-getpid(), SIGKILL);
    abort();
  }
  sigFaulting = true;

  fmt::print("WARNING: unexpected signal %d received. Dumping partial statistics\n", sig);
  signal(SIGUSR1, signalCatcher);  // Even sigusr1 should go here

  BootLoader::reportOnTheFly();

  BootLoader::unboot();
  BootLoader::unplug();

  sigFaulting = false;

  abort();
}

timeval BootLoader::stTime;

void BootLoader::reportOnTheFly() {
  Report::field("partial=true");

  Report::reinit();
  Config::dump(Report::raw_file_descriptor());

  // pwrmodel->startDump();
  // pwrmodel->stopDump();
}

void BootLoader::report(std::string_view str) {
  timeval endTime;
  gettimeofday(&endTime, 0);

  Report::field(fmt::format("#BEGIN:report {}", str));
  Report::field(fmt::format("OSSim:reportName={}", str));
  Report::field(fmt::format("OSSim:beginTime={}", ctime(&stTime.tv_sec)));
  Report::field(fmt::format("OSSim:endTime={}", ctime(&endTime.tv_sec)));

  double msecs = (endTime.tv_sec - stTime.tv_sec) * 1000 + (endTime.tv_usec - stTime.tv_usec) / 1000;

  TaskHandler::report();

  Report::field(fmt::format("OSSim:msecs={}", (double)msecs / 1000));

  Stats::report_all();

  Report::field(fmt::format("#END:report {}", str));
  Report::close();
}

void BootLoader::plug_emuls() {

  srandom(100); // No randomize

  auto nemuls = Config::get_array_size("soc", "emul");

  std::shared_ptr<Emul_dromajo> dromajo;

  for (auto i = 0u; i < nemuls; i++) {
    auto type = Config::get_string("soc", "emul", i, "type", {"dromajo", "accel", "trace"});
    if (type == "dromajo") {
      if (dromajo == nullptr) {
        dromajo = std::make_shared<Emul_dromajo>();
      }
      if (dromajo) {  // Invalid dromahor otherwise
        TaskHandler::add_emul(dromajo, i);
      }
    } else if (type == "accel") {
      Config::add_error("accel still not implemented");
    } else if (type == "trace") {
      Config::add_error("trace still not implemented");
    }
  }
}

void BootLoader::plug_simus() {
  auto ncores = Config::get_array_size("soc", "core");

  for (auto i = 0u; i < ncores; i++) {
    std::shared_ptr<Gmemory_system> gm;
    auto                            caches = Config::get_bool("soc", "core", i, "caches");
    if (caches) {
      gm = std::make_shared<Memory_system>(i);
    } else {
      gm = std::make_shared<Dummy_memory_system>(i);
    }

    auto                       type = Config::get_string("soc", "core", i, "type", {"ooo", "inorder", "accel"});
    std::shared_ptr<Simu_base> simu;
    if (type == "ooo") {
      simu = std::make_shared<OoOProcessor>(gm, i);
    } else if (type == "inorder") {
      simu = std::make_shared<InOrderProcessor>(gm, i);
    } else if (type == "accel") {
      simu = std::make_shared<AccProcessor>(gm, i);
    }
    TaskHandler::simu_create(simu);
  }
}

void BootLoader::plug(int argc, const char **argv) {
  // Before boot

  std::string conf_file  = "desesc.toml";
  bool        just_check = false;

  for (auto i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "-c") == 0) {
      ++i;
      if (i >= argc) {
        fmt::print("after -c, there should be a config file name\n");
        exit(-3);
      }
      conf_file = argv[i];
    } else if (strcasecmp(argv[i], "check") == 0) {
      just_check = true;
    } else {
      fmt::print("unknown {} command line option\n", argv[i]);
      exit(-3);
    }
  }
  Config::init(conf_file);

  auto ncores = Config::get_array_size("soc", "core");
  auto nemuls = Config::get_array_size("soc", "emul");

  if (ncores != nemuls) {
    Config::add_error(fmt::format("soc number of cores should match the numbers of emuls ({} vs {})", ncores, nemuls));
  } else if (ncores == 0) {
    Config::add_error("soc should have at least one core in [soc] core");
  } else {
    TaskHandler::plugBegin();
    plug_simus();

    Config::exit_on_error();
  }

  if (just_check) {
    fmt::print("check success\n");
    exit(0);
  }

  plug_emuls();

  Config::exit_on_error();

  arch.drawArchDot("memory-arch.dot");

#if 0
  if (Config::has_entry("soc","pwdmode")) {
    auto pwdsection = Config::get_string("soc", "pwrmodel");
    pwrmodel->plug(pwrsection);
    check();
  }
#endif

  TaskHandler::plugEnd();
}

void BootLoader::boot() {
  gettimeofday(&stTime, 0);

  Config::exit_on_error();

  Report::init();
  Config::dump(Report::raw_file_descriptor());

  TaskHandler::boot();
}

void BootLoader::unboot() {
#ifdef ESESC_THERM
  ReportTherm::stopCB();
#endif

  TaskHandler::unboot();
}

void BootLoader::unplug() {
  // after unboot

#ifdef ESESC_THERM
  ReportTherm::stopCB();
  ReportTherm::close();
#endif

#ifdef ESESC_POWERMODEL
  if (doPower) {
    pwrmodel->unplug();
  }
#endif

  TaskHandler::unplug();
}
