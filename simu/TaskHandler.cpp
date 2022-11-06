// See LICENSE for details.

#include "TaskHandler.h"

#include <string.h>

#include <iostream>

#include "GProcessor.h"
#include "Report.h"
#include "config.hpp"
#include "emul_base.hpp"

std::vector<TaskHandler::EmulSimuMapping> TaskHandler::allmaps;
volatile bool                             TaskHandler::terminate_all;
pthread_mutex_t                           TaskHandler::mutex;
pthread_mutex_t                           TaskHandler::mutex_terminate;

Hartid_t *TaskHandler::running;
Hartid_t  TaskHandler::running_size;

std::vector<Emul_base  *> TaskHandler::emulas;  // associated emula
std::vector<GProcessor *> TaskHandler::cpus;    // All the CPUs in the system

void TaskHandler::report(const char *str) {
  /* dump statistics to report file {{{1 */

  Report::field("OSSim:nCPUs=%d", cpus.size());
  size_t cpuid        = 0;
  size_t cpuid_sub    = 0;

  for (size_t i = 0; i < emulas.size(); i++) {
    Report::field("OSSim:P(%d)_Type=%d", cpuid, emulas[i]->cputype);

    cpuid_sub++;
    // if (cpuid_sub>=cpus[cpuid]->getMaxFlows()) {
    cpuid_sub = 0;
    GI((cpus.size() > 1), (cpuid < cpus.size()));
    cpuid = cpuid + 1;
    //}
  }

  Report::field("OSSim:globalClock=%lld", globalClock);
}
/* }}} */

void TaskHandler::addEmul(Emul_base *eint, Hartid_t fid) {
  /* add a new emulator to the system {{{1 */

  Hartid_t nemul = Config::get_array_size("soc", "core");

  if (emulas.empty()) {
    for (Hartid_t i = 0; i < nemul; i++) {
      emulas.push_back(0x0);
    }
  }

  I(!emulas.empty());

  emulas.erase(emulas.begin() + fid);
  emulas.insert(emulas.begin() + fid, eint);
}
/* }}} */

void TaskHandler::addEmulShared(Emul_base *eint) {
  /* add a new emulator to the system {{{1 */

  Hartid_t nemul = Config::get_array_size("soc", "core");

  if (emulas.empty()) {
    for (Hartid_t i = 0; i < nemul; i++) {
      emulas.push_back(0x0);
    }
  }

  I(!emulas.empty());

  for (Hartid_t i = 0; i < nemul; i++) {
    const char *section     = Config::get_string("soc", "emul", i);
    const char *eintsection = eint->getSection();
    if (strcasecmp(section, eintsection) == 0) {
      emulas.erase(emulas.begin() + i);
      emulas.insert(emulas.begin() + i, eint);
    }
  }

  /*
  // IF DEBUG
  for(Hartid_t i=0;i<nemul;i++)
  {
    if (emulas.at(i) != 0x0)
      MSG("Emul[%d] = %s", i, (emulas.at(i))->getSection());
  }
  */
}
/* }}} */

void TaskHandler::addSimu(GProcessor *gproc) {
  /* add a new simulator to the system {{{1 */
  I(cpus.size() == static_cast<size_t>(gproc->getID()));
  cpus.push_back(gproc);

  EmulSimuMapping map;

  map.fid          = gproc->getID();
  map.emul         = 0;
  map.simu         = gproc;
  map.active       = gproc->isActive();
  map.deactivating = false;

  allmaps.push_back(map);
}
/* }}} */

Hartid_t TaskHandler::resumeThread(Hartid_t uid, Hartid_t fid) {
  /* activate fid {{{1 */

  // I(uid<65535); // No more than 65K threads for the moment
  pthread_mutex_lock(&mutex);

  I(allmaps[fid].fid == fid);
  I(!allmaps[fid].active);

  // IS(MSG("TaskHandler::CPU resumeThread(%d)",fid));

  if (allmaps[fid].active || terminate_all) {
    if (terminate_all)
      MSG("TaskHandler::terminate_all flag is set, cannot start Thread (%d)", fid);
    /*
        fprintf(stderr,"CPUResume(%d): running_size = %d : running->",fid,running_size);
        for (int i = 0; i < running_size; i++)
          fprintf(stderr,"%d->",running[i]);
        fprintf(stderr,"\n");

    */
    pthread_mutex_unlock(&mutex);
    return (0);
  }
  allmaps[fid].active       = true;
  allmaps[fid].deactivating = false;
  allmaps[fid].simu->setActive();
  bool found = false;
  for (int i = 0; i < running_size; i++) {
    if (running[i] == fid) {
      found = true;
      break;
    }
  }

  if (!found) {
    allmaps[fid].active = true;
    I(running_size < allmaps.size());
    running[running_size] = fid;
    running_size++;
  }

#ifndef NDEBUG
  fprintf(stderr, "CPUResume: fid=%d running_size=%d running=", fid, running_size);
  for (int i = 0; i < running_size; i++) fprintf(stderr, "%d:", running[i]);
  fprintf(stderr, "\n");
#endif

  pthread_mutex_unlock(&mutex);
  return (fid);
}

/* }}} */

Hartid_t TaskHandler::resumeThread(Hartid_t fid) {
  /* activate fid, for GPU flow {{{1 */

  I(fid < 65535);  // No more than 65K flows for the moment

  pthread_mutex_lock(&mutex);
  for (size_t i = 0; i < running_size; i++) {
    if (running[i] == fid) {
      // MSG("Was in queue");
      allmaps[fid].active       = true;
      allmaps[fid].deactivating = false;
      allmaps[fid].simu->setActive();
      /*
      fprintf(stderr,"GPUResume: running_size = %d : running->",running_size);
      for (int i = 0; i < running_size; i++)
        fprintf(stderr,"%d->",running[i]);
      fprintf(stderr,"\n");
      */
      pthread_mutex_unlock(&mutex);
      return (fid);
    }
  }

  pthread_mutex_unlock(&mutex);

  Hartid_t GPU_fid = (fid);
  I(allmaps[GPU_fid].fid == GPU_fid);

  pthread_mutex_lock(&mutex);
  //  if (allmaps[GPU_fid].active || terminate_all){
  if (terminate_all) {
    MSG("TaskHandler::terminate_all flag is set, cannot start Thread (%d)", fid);
    pthread_mutex_unlock(&mutex);
    return (999);
  } else if (allmaps[GPU_fid].active) {
    MSG("TaskHandler::fid(%d) is already active", GPU_fid);
    pthread_mutex_unlock(&mutex);
    return (GPU_fid);
  }

  allmaps[GPU_fid].active       = true;
  allmaps[GPU_fid].deactivating = false;
  allmaps[GPU_fid].simu->setActive();

  // Make sure that the fid is not in the running queue
  // This might happen if the execution is very slow.

  running[running_size] = GPU_fid;
  running_size++;

  fprintf(stderr, "CPUResume: fid=%d running_size=%d running=", fid, running_size);
  for (int i = 0; i < running_size; i++) fprintf(stderr, "%d:", running[i]);
  fprintf(stderr, "\n");

  //  allmaps[GPU_fid].emul->getSampler()-> startMode(GPU_fid);
  pthread_mutex_unlock(&mutex);
  return (GPU_fid);
}
/* }}} */

void TaskHandler::removeFromRunning(Hartid_t fid) {
  /* remove fid from the running queue {{{1 */

  I(allmaps[fid].simu->isROBEmpty());

  for (size_t i = 0; i < running_size; i++) {
    if (running[i] != fid)
      continue;

    if (i < running_size - 1) {
      for (size_t j = i; j < running_size - 1; j++) {
        running[j] = running[j + 1];
      }
    }
    running_size--;

#ifndef NDEBUG
    fprintf(stderr, "removeFromRunning: fid=%d running_size=%d : running=", fid, running_size);
    for (int j = 0; j < running_size; j++) fprintf(stderr, "%d:", running[j]);
    fprintf(stderr, "\n");
#endif

    break;
  }
}
/* }}} */

void TaskHandler::pauseThread(Hartid_t fid) {
  /* deactivae an fid {{{1 */

  fprintf(stderr, "P");
  I(allmaps[fid].fid == fid);
  I(fid < 65535);
  if (terminate_all)
    return;

  pthread_mutex_lock(&mutex);

  if (!allmaps[fid].active) {
    // MSG("TaskHandler::pauseThread(%d) not needed, since it is not active",fid);
    if (allmaps[fid].deactivating) {
      if (allmaps[fid].simu->isROBEmpty()) {
        removeFromRunning(fid);
        allmaps[fid].active       = false;
        allmaps[fid].deactivating = false;  // already deactivated
        allmaps[fid].simu->clearActive();
      }
    }
    pthread_mutex_unlock(&mutex);
    return;
  }

  allmaps[fid].active       = false;  // So that no more populate is called
  allmaps[fid].deactivating = true;

  pthread_mutex_unlock(&mutex);
}
/* }}} */

void TaskHandler::terminate()
/* deactivae an fid {{{1 */
{
  terminate_all = true;

  MSG("TaskHandler::terminate");

  for (size_t i = 0; i < allmaps.size(); i++) {
    if (!allmaps[i].active)
      continue;
    if (allmaps[i].emul)
      allmaps[i].emul->getSampler()->stop();
    allmaps[i].active       = false;
    allmaps[i].deactivating = false;
  }

  // GStats::stopAll(1);

  running_size = 0;

  // LOCK thread until TaskHandler::unplug is called (otherwise, there could be a race)
  fflush(stdout);
  fflush(stderr);
  pthread_mutex_lock(&mutex_terminate);
}
/* }}} */

void TaskHandler::freeze(Hartid_t fid, Time_t nCycles) {
  /* put a core to sleep (thermal emergency?) fid {{{1 */
  I(allmaps[fid].active);
  I(fid < 65535);  // No more than 65K threads for the moment
  allmaps[fid].simu->freeze(nCycles);
}
/* }}} */

extern "C" void helper_esesc_dump();
void            TaskHandler::boot()
/* main simulation loop {{{1 */
{
  while (!terminate_all) {
    if (unlikely(running_size == 0)) {
      bool needIncreaseClock = false;
      for (AllMapsType::iterator it = allmaps.begin(); it != allmaps.end(); it++) {
        if (it->emul == 0)
          continue;
        EmuSampler::EmuMode m = (*it).emul->getSampler()->getMode();
        if (m == EmuSampler::EmuDetail || m == EmuSampler::EmuTiming) {
          needIncreaseClock = true;
          break;
        }
      }
      if (needIncreaseClock)
        EventScheduler::advanceClock();
    } else {
      // 1st Make sure that they have enough instructions
      bool one_failed;
      bool all_failed;
      bool retry = false;
      do {
#ifndef NDEBUG
        static int conta = 0;
        conta++;
        if (conta > 1000000) {
          fprintf(stderr, ".");
          conta = 0;
        }
#endif
        do {
          one_failed = false;
          all_failed = true;
          for (size_t i = 0; i < running_size; i++) {
            Hartid_t fid = running[i];
            if (allmaps[fid].emul == 0) {
              all_failed = false;
              continue;
            }
            bool p = allmaps[fid].emul->populate(fid);
            if (!p)
              one_failed = true;
            else
              all_failed = false;
          }
        } while (all_failed && running_size);
        if (!one_failed)
          break;
        if (retry) {
          for (size_t i = 0; i < running_size; i++) {
            Hartid_t fid = running[i];
            if (!allmaps[fid].active) {
              if (!allmaps[fid].deactivating)
                removeFromRunning(fid);
              continue;
            }

            if (allmaps[fid].emul) {
              bool p = allmaps[fid].emul->populate(fid);
              if (!p)
                pauseThread(fid);
            }
          }
          break;
        }
        retry = true;
      } while (running_size);
      // 2nd: advance cores
      for (size_t i = 0; i < running_size; i++) {
        Hartid_t fid = running[i];
        if (allmaps[fid].deactivating) {
#ifndef NDEBUG
          if (!allmaps[fid].simu->isROBEmpty()) {
            MSG("@%lld drain fid:%d rob:%d", globalClock, fid, allmaps[fid].simu->getROBsize());
          }
#endif
          allmaps[fid].simu->drain();
          if (allmaps[fid].simu->isROBEmpty()) {
            pauseThread(fid);
            I(!allmaps[fid].deactivating || terminate_all);
          }
        } else {
          allmaps[fid].simu->advance_clock(fid);
        }
      }
#ifndef NDEBUG
      for (size_t i = 0; i < allmaps.size(); i++) {
        if (allmaps[i].active)
          continue;
        if (allmaps[i].deactivating)
          continue;
        I(allmaps[i].simu->isROBEmpty());
      }
#endif
      EventScheduler::advanceClock();
    }
  }
}
/* }}} */

void TaskHandler::unboot()
/* nothing to do {{{1 */
{}
/* }}} */

void TaskHandler::plugBegin()
/* allocate objects {{{1 */
{
  I(emulas.empty());
  I(cpus.empty());
  terminate_all = false;

  running      = nullptr;
  running_size = 0;

  pthread_mutex_lock(&mutex_terminate);
}
/* }}} */

void TaskHandler::plugEnd()
/* setup running and allmaps before starting the main loop {{{1 */
{
  size_t nCPUThreads = 0;
  for (size_t i = 0; i < cpus.size(); i++) {
    nCPUThreads += 1;  // cpus[i]->getMaxFlows();
  }
  if (emulas.size() > nCPUThreads) {
    Config::add_error(fmt::format("There are more emul ({}) than cpu flows ({}) available. Increase the number of cores or emulas can starve",
        emulas.size(),
        nCPUThreads);
  } else if (emulas.size() < nCPUThreads) {
    if (emulas.size() != 0)
      fmt::print("Warning: There are more cores than threads ({} vs {}). Powering down unusable cores\n", emulas.size(), nCPUThreads);
  }

  // Tie the emulas to the all maps
  size_t cpuid     = 0;
  size_t cpuid_sub = 0;

  for (size_t i = 0; i < emulas.size(); i++) {
    allmaps[i].fid  = static_cast<Hartid_t>(i);
    allmaps[i].emul = emulas[i];
    I(allmaps[i].simu == cpus[cpuid]);

    if (i == 0) {
      I(allmaps[i].active == true);  // active by default
    } else {
      I(allmaps[i].active == false);
    }

    allmaps[i].simu->set_emul(emulas[i]);

    cpuid_sub++;
    // if (cpuid_sub>=cpus[cpuid]->getMaxFlows()) {
    cpuid_sub = 0;
    I(cpuid < cpus.size());
    cpuid = cpuid + 1;
    // }
  }
  for (size_t i = 0; i < allmaps.size(); i++) {
    if (allmaps[i].active)
      running_size++;
  }
  I(running_size > 0);
  /*************************************************/

  running         = new Hartid_t[allmaps.size()];
  int running_pos = 0;
  for (size_t i = 0; i < allmaps.size(); i++) {
    if (allmaps[i].emul)
      allmaps[i].emul->start();
    if (allmaps[i].active)
      running[running_pos++] = i;
  }
  I(running_pos == running_size);
}
/* }}} */

void TaskHandler::unplug()
/* delete objects {{{1 */
{
#ifdef WAVESNAP_EN
  for (size_t i = 0; i < cpus.size(); i++) {
    if (i == 0) {
      std::cout << "Done! Getting wavesnap info." << std::endl;
      if (SINGLE_WINDOW) {
        cpus[i]->snap->calculate_single_window_ipc();
      } else {
        cpus[i]->snap->calculate_ipc();
        cpus[i]->snap->window_frequency();
      }
    }
  }
#endif

#if 0
  for(size_t i=0; i<cpus.size() ; i++) {
    delete cpus[i];
  }

  for(size_t i=0; i<emulas.size() ; i++) {
    delete emulas[i];
  }
#endif

  pthread_mutex_unlock(&mutex_terminate);
}
/* }}} */

Hartid_t TaskHandler::getNumActiveCores() {
  Hartid_t numActives = 0;
  for (size_t i = 0; i < allmaps.size(); i++) {
    if (allmaps[i].active)
      numActives++;
  }
  return numActives;
}
