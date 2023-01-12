// See license for details

#include "fmt/format.h"

#include "config.hpp"
#include "tracer.hpp"
#include "report.hpp"
#include "iassert.hpp"

bool Tracer::open(const std::string &fname) {

  auto file_name = absl::StrCat(fname, ".", Report::get_extension());

  ofs.open(file_name);
  if (!ofs) {
    Config::add_error(fmt::format("unable to open trace file {}", file_name));
    track_from = UINT64_MAX;
    track_to   = UINT64_MAX;
    return false;
  }

  main_clock_set = false;
  last_clock     = 0;
  track_from     = 0;
  track_to       = UINT64_MAX;

  return true;
}

void Tracer::track_range(uint64_t from, uint64_t to) {
  I(ofs);

  track_from = from;
  track_to   = to;
}

void Tracer::stage(const Dinst *dinst, const std::string ev) {
  I(ev.size()<4); // tracer stages should have 4 or less characers

  if (dinst->getID() > track_to || dinst->getID() < track_from)
    return;

  adjust_clock();

  if (!started.contains(dinst->getID())) {
    ofs << "I " << dinst->getID() << " " << dinst->getID() << " " << dinst->getFlowId() << "\n";
    ofs << "L " << dinst->getID() << " 0 " << std::hex << dinst->getPC() << " " << dinst->getInst()->get_asm() << "\n";
    started.insert(dinst->getID());
  }

  ofs << "S " << dinst->getID() << " 0 " << ev << "\n";
}

void Tracer::event(const Dinst *dinst, const std::string ev) {
  I(ev.size()<8); // tracer events should have 8 or less characers

  if (dinst->getID() > track_to || dinst->getID() < track_from)
    return;

  I(started.contains(dinst->getID())); // events should be called once an instruction is already started

  adjust_clock();

  ofs << fmt::format("S {} 1 {}\n", dinst->getID(), ev);

  pending_end.emplace_back(fmt::format("E {} 1 {}\n", dinst->getID(), ev));
}

void Tracer::commit(const Dinst *dinst) {
  if (dinst->getID() > track_to || dinst->getID() < track_from)
    return;

  adjust_clock();

  ofs << fmt::format("R {} {} 0\n", dinst->getID(), dinst->getID());
}

void Tracer::flush(const Dinst *dinst) {
  if (dinst->getID() > track_to || dinst->getID() < track_from)
    return;

  adjust_clock();

  ofs << fmt::format("R {} {} 1\n", dinst->getID(), dinst->getID());
}

void Tracer::adjust_clock() {
  if (!pending_end.empty()) {
    I(main_clock_set);
    for (const auto &txt:pending_end) {
      ofs << txt;
    }
    pending_end.clear();
  }

  if (!main_clock_set) {
    ofs << "C= " << globalClock << "\n";
    main_clock_set = true;
    last_clock = globalClock;
  }else if (last_clock != globalClock) {
    ofs << "C " << globalClock-last_clock << "\n";
    last_clock = globalClock;
  }
}

