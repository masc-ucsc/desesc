// See LICENSE for details.

#include <pthread.h>
#include <signal.h>
#include <stdlib.h>

#include "BootLoader.h"
#include "MemorySystem.h"
#include "AccProcessor.h"
#include "GMemorySystem.h"
#include "GPUSMProcessor.h"
#include "GProcessor.h"
#include "InOrderProcessor.h"
#include "OoOProcessor.h"
#include "DrawArch.h"

#include "report.hpp"
#include "config.hpp"

extern DrawArch arch;

extern "C" void signalCatcher(int32_t sig);

extern "C" void signalCatcherUSR1(int32_t sig) {

  MSG("WARNING: signal %d received. Dumping partial statistics\n", sig);

  BootLoader::reportOnTheFly();

  signal(SIGUSR1, signalCatcherUSR1);
}

extern "C" void signalCatcher(int32_t sig) {

  MSG("Stopping simulation early");

  static bool sigFaulting = false;
  if(sigFaulting) {
    TaskHandler::unplug();
    MSG("WARNING. Not a nice stop. It may leave pids");
    kill(-getpid(), SIGKILL);
    abort();
  }
  sigFaulting = true;

  MSG("WARNING: unexpected signal %d received. Dumping partial statistics\n", sig);
  signal(SIGUSR1, signalCatcher); // Even sigusr1 should go here

  BootLoader::reportOnTheFly();

  BootLoader::unboot();
  BootLoader::unplug();

  sigFaulting = false;

  abort();
}

char *      BootLoader::reportFile;
timeval     BootLoader::stTime;
PowerModel *BootLoader::pwrmodel;
bool        BootLoader::doPower;

void BootLoader::check() {
  if(Config::has_errors()) {
    exit(-1);
  }
}

void BootLoader::reportOnTheFly() {

  Report::field("partial=true");

  Report::reinit();
  Config::dump(Report::raw_file_descriptor());

  pwrmodel->startDump();
  pwrmodel->stopDump();
}

void BootLoader::report(const char *str) {
  timeval endTime;
  gettimeofday(&endTime, 0);

  Report::field("OSSim:reportName=%s", str);
  Report::field("OSSim:beginTime=%s", ctime(&stTime.tv_sec));
  Report::field("OSSim:endTime=%s", ctime(&endTime.tv_sec));

  double msecs = (endTime.tv_sec - stTime.tv_sec) * 1000 + (endTime.tv_usec - stTime.tv_usec) / 1000;

  TaskHandler::report(str);

  Report::field("OSSim:msecs=%8.2f", (double)msecs / 1000);

  GStats::report(str);

  Report::close();
}

void BootLoader::reportSample() {
}

void BootLoader::plugEmulInterfaces() {

  FlowID nemul = SescConf->getRecordSize("", "cpuemul");

  // nemul will give me the total number of flows (cpuemuls)  in the system.
  // out of these, some can be CPU, some can be GPU. (interleaved as well)
  I(nemul > 0);

  LOG("I: cpuemul size [%d]", nemul);

  // For now, we will assume the simplistic case where there is one QEMU and one GPU.
  // (So one object each of classes QEMUEmulInterface and GPUEmulInterface)
  const char *QEMUCPUSection = NULL;
  for(FlowID i = 0; i < nemul; i++) {
    const char *section = SescConf->getCharPtr("", "cpuemul", i);
    const char *type    = SescConf->getCharPtr(section, "type");

    if(strcasecmp(type, "QEMU") == 0) {
      if(QEMUCPUSection == NULL) {
        QEMUCPUSection = section;
      } else if(strcasecmp(QEMUCPUSection, section)) {
        MSG("ERROR: eSESC supports only a single instance of QEMU");
        MSG("cpuemul[%d] specifies a different section %s", i, section);
        SescConf->notCorrect();
        return;
      }
      createEmulInterface(QEMUCPUSection, i); // each CPU has it's own Emul/Sampler
    } else if(strcasecmp(type, "accel") == 0) {
      MSG("cpuemul[%d] specifies a different section %s", i, section);
    } else {
      MSG("ERROR: Unknown type %s of section %s, cpuemul [%d]", type, section, i);
      SescConf->notCorrect();
      return;
    }
  }
}

void BootLoader::plugSimuInterfaces() {

  FlowID nsimu = SescConf->getRecordSize("", "cpusimu");

  LOG("I: cpusimu size [%d]", nsimu);

  for(FlowID i = 0; i < nsimu; i++) {
    const char *section = SescConf->getCharPtr("", "cpusimu", i);
    createSimuInterface(section, i);
  }
}

EmuSampler *BootLoader::getSampler(const char *section, const char *keyword, EmulInterface *eint, FlowID fid) {
  const char *sampler_sec  = SescConf->getCharPtr(section, keyword);
  const char *sampler_type = SescConf->getCharPtr(sampler_sec, "type");

  static EmuSampler *sampler = 0;

  if(sampler)
    return sampler;

  if(strcasecmp(sampler_type, "inst") == 0) {
    sampler = new SamplerSMARTS("TASS", sampler_sec, eint, fid);
  } else if(strcasecmp(sampler_type, "sync") == 0) {
    sampler = new SamplerSync("SYNC", sampler_sec, eint, fid);
  } else if(strcasecmp(sampler_type, "time") == 0) {
    sampler = new SamplerPeriodic("TBS", sampler_sec, eint, fid);
  } else {
    MSG("ERROR: unknown sampler [%s] type [%s]", sampler_sec, sampler_type);
    SescConf->notCorrect();
  }

  return sampler;
}

void BootLoader::createEmulInterface(const char *section, FlowID fid) {
#ifndef ENABLE_NOEMU
  const char *type = SescConf->getCharPtr(section, "type");

  if(type == 0) {
    MSG("ERROR: type field should be defined in section [%s]", section);
    SescConf->notCorrect();
    return;
  }

  EmulInterface *eint = 0;
  if(strcasecmp(type, "QEMU") == 0) {
    eint = new QEMUEmulInterface(section);
    TaskHandler::addEmul(eint, fid);
  } else {
    MSG("ERROR: unknown cpusim [%s] type [%s]", section, type);
    SescConf->notCorrect();
    return;
  }
  I(eint);

  EmuSampler *sampler = getSampler(section, "sampler", eint, fid);
  I(sampler);
  eint->setSampler(sampler, fid);
#endif
}

void BootLoader::createSimuInterface(const char *section, FlowID i) {

  GMemorySystem *gms = 0;
  if(SescConf->checkInt(section, "noMemory")) {
    gms = new DummyMemorySystem(i);
  } else {
    gms = new MemorySystem(i);
  }
  gms->buildMemorySystem();

  CPU_t cpuid = static_cast<CPU_t>(i);


  auto type = Config::get_string("soc", "core", cpuid, "type", {"inorder", "ooo", "accel"});
  if (type.empty())
    return;

  GProcessor *gproc = 0;
  if(type == "inorder")    { gproc = new InOrderProcessor(gms, cpuid); }
  else if(type == "accel") { gproc = new AccProcessor(gms, cpuid); }
  else if(type == "ooo")   { gproc = new OoOProcessor(gms, cpuid); }
  else {
    I(false);
  }

  I(gproc);
  TaskHandler::addSimu(gproc);
}

void BootLoader::plug(int argc, const char **argv) {
  // Before boot

  Config::init();  // FIXME: -c desesc2.conf

  TaskHandler::plugBegin();
  plugSimuInterfaces();

  check();

  if(argc > 1 && strcmp(argv[1], "check") == 0) {
    printf("success\n");
    exit(0);
  }


  plugEmulInterfaces();
  check();

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

  if(!Config::has_errors())
    exit(-1);

  Report::init();
  Config::dump(Report::raw_file_descriptor());

  TaskHandler::boot();
}

void BootLoader::unboot() {

  MSG("BootLoader::unboot called... Finishing the work");

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
  if(doPower)
    pwrmodel->unplug();
#endif

  TaskHandler::unplug();
}
