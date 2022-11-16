// See LICENSE for details.

#include "TaskHandler.h"

#include <string.h>

#include <iostream>

#include "GProcessor.h"
#include "config.hpp"
#include "emul_base.hpp"
#include "report.hpp"

void TaskHandler::report() {
  /* dump statistics to report file {{{1 */

  Report::field(fmt::format("OSSim:nCPUs={}", simus.size()));

  for (size_t i = 0; i < emuls.size(); i++) {
    Report::field(fmt::format("OSSim:P({})emul_type={}", i, emuls[i]->get_type()));
    Report::field(fmt::format("OSSim:P({})simu_type={}", i, simus[i]->get_type()));
  }

  Report::field(fmt::format("OSSim:global_clock={}", globalClock));
}
/* }}} */

void TaskHandler::add_emul(std::shared_ptr<Emul_base> eint, Hartid_t hid) {
  I(hid < 65535);  // Too many cores

  if (hid > emuls.size()) {
    emuls.resize(hid + 1, nullptr);
  }

  emuls[hid] = eint;
}

void TaskHandler::core_create(std::shared_ptr<GProcessor> gproc) {
  I(plugging);  // It can be called only during booting

  I(simus.size() == static_cast<size_t>(gproc->get_hid()));

  for(auto i=0;i<gproc->get_smt_size();++i) {
    simus.push_back(gproc);

    EmulSimuMapping map;

    map.fid          = gproc->get_hid();
    map.emul         = nullptr;
    map.active       = gproc->is_power_up();
    map.simu         = gproc;
    map.deactivating = false;

    allmaps.push_back(map);
  }
}

void TaskHandler::core_resume(Hartid_t fid) {
  I(fid < 65535);  // No more than 65K flows for the moment

  if (running.contains(fid)) {
    allmaps[fid].active       = true;
    allmaps[fid].deactivating = false;
    allmaps[fid].simu->set_power_up();
    return;
  }

  if (terminate_all || allmaps[fid].active) {
    return;
  }

  allmaps[fid].active       = true;
  allmaps[fid].deactivating = false;
  allmaps[fid].simu->set_power_up();

  running.insert(fid);
}
/* }}} */

void TaskHandler::core_pause(Hartid_t fid) {
  /* deactivae an fid {{{1 */

  fprintf(stderr, "P");
  I(allmaps[fid].fid == fid);
  I(fid < 65535);
  if (terminate_all)
    return;

  if (allmaps[fid].active) {
    allmaps[fid].active       = false;  // So that no more populate is called
    allmaps[fid].deactivating = true;
  }
}
/* }}} */

void TaskHandler::core_terminate_all()
/* deactivae an fid {{{1 */
{
  terminate_all = true;

  for (size_t i = 0; i < allmaps.size(); i++) {
    if (!allmaps[i].active)
      continue;
    allmaps[i].active       = false;
    allmaps[i].deactivating = false;
  }

  running.clear();
}
/* }}} */

void TaskHandler::core_freeze(Hartid_t fid, Time_t nCycles) {
  I(allmaps[fid].active);
  I(fid < 65535);  // No more than 65K threads for the moment
  allmaps[fid].simu->freeze(nCycles);
}

void TaskHandler::boot() {
  while (!running.empty()) {
    // advance cores & check for deactivate
    for (auto hid : running) {
      if (likely(!allmaps[hid].deactivating)) {
        allmaps[hid].simu->advance_clock();
      } else {
        auto work_done = allmaps[hid].simu->advance_clock_drain();
        if (!work_done) {
          allmaps[hid].active       = false;
          allmaps[hid].deactivating = false;  // already deactivated
          allmaps[hid].simu->set_power_down();

          running.erase(hid);
          break;  // core_pause can break the iterator
        }
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

void TaskHandler::unboot()
/* nothing to do {{{1 */
{}
/* }}} */

void TaskHandler::plugBegin()
/* allocate objects {{{1 */
{
  I(emuls.empty());
  I(simus.empty());
  terminate_all = false;
  plugging      = true;

  running.clear();
}
/* }}} */

void TaskHandler::plugEnd()
/* setup running and allmaps before starting the main loop {{{1 */
{
  size_t nCPUThreads = 0;
  for (size_t i = 0; i < simus.size(); i++) {
    nCPUThreads += 1;
  }
  if (emuls.size() > nCPUThreads) {
    Config::add_error(
        fmt::format("There are more emul ({}) than cpu flows ({}) available. Increase the number of cores or emuls can starve",
                    emuls.size(),
                    nCPUThreads));
  } else if (emuls.size() < nCPUThreads) {
    if (emuls.size() != 0)
      fmt::print("Warning: There are more cores than threads ({} vs {}). Powering down unusable cores\n",
                 emuls.size(),
                 nCPUThreads);
  }

  // Tie the emuls to the all maps
  size_t cpuid     = 0;
  size_t cpuid_sub = 0;

  for (size_t i = 0; i < emuls.size(); i++) {
    allmaps[i].fid  = static_cast<Hartid_t>(i);
    allmaps[i].emul = emuls[i];
    I(allmaps[i].simu == simus[cpuid]);

    I(allmaps[i].active == true);  // active by default
    running.insert(i);

    allmaps[i].simu->set_emul(emuls[i]);

    cpuid_sub++;
    cpuid_sub = 0;
    I(cpuid < simus.size());
    cpuid = cpuid + 1;
  }

  plugging = false;
}
/* }}} */

void TaskHandler::unplug()
/* delete objects {{{1 */
{
#ifdef WAVESNAP_EN
  for (size_t i = 0; i < simus.size(); i++) {
    if (i == 0) {
      std::cout << "Done! Getting wavesnap info." << std::endl;
      if (SINGLE_WINDOW) {
        simus[i]->snap->calculate_single_window_ipc();
      } else {
        simus[i]->snap->calculate_ipc();
        simus[i]->snap->window_frequency();
      }
    }
  }
#endif
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
