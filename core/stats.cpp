// See LICENSE for details

#include "stats.hpp"

#include "config.hpp"
#include "fmt/format.h"
#include "report.hpp"

/*********************** Stats */

Stats::~Stats() { unsubscribe(); }

void Stats::subscribe() {
  I(!name.empty());

  if (store.find(name) != store.end()) {
    Config::add_error(fmt::format("gstats is added twice with name [{}]. Use another name", name));
    return;
  }

  store[name] = this;
}

void Stats::unsubscribe() {
  I(!name.empty());

  auto it = store.find(name);
  if (it != store.end()) {
    store.erase(it);
  }
}

void Stats::report_all() {
  Report::field(fmt::format("#BEGIN Stats"));

  for (const auto &e : store) {
    e.second->report();
  }

  Report::field(fmt::format("#END Stats"));
}

void Stats::reset_all() {
  for (auto &e : store) {
    e.second->reset();
  }
}

/*********************** Stats_pwr */

Stats_pwr::Stats_pwr(const std::string &str) : Stats(str) { subscribe(); }

void Stats_pwr::report() const { Report::field(fmt::format("{}:real={} tran={}\n", name, cntr_real, cntr_tran)); }

void Stats_pwr::reset() {
  cntr_tran = 0;
  cntr_real = 0;
}

/*********************** Stats_cntr */

Stats_cntr::Stats_cntr(const std::string &str) : Stats(str) {
  data = 0;

  subscribe();
}

void Stats_cntr::report() const { Report::field(fmt::format("{}={}\n", name, data)); }

void Stats_cntr::reset() { data = 0; }

/*********************** Stats_avg */

Stats_avg::Stats_avg(const std::string &str) : Stats(str) {
  data  = 0;
  nData = 0;

  subscribe();
}

void Stats_avg::sample(const double v, bool en) {
  data += en ? v : 0;
  nData += en ? 1 : 0;
}

void Stats_avg::report() const {
  auto v = data / nData;

  Report::field(fmt::format("{}:n={}::v={}\n", name, nData, v));  // n first for power
}

void Stats_avg::reset() {
  data  = 0;
  nData = 0;
}

/*********************** Stats_max */

Stats_max::Stats_max(const std::string &str) : Stats(str) {
  maxValue = 0;
  nData    = 0;

  subscribe();
}

void Stats_max::report() const { Report::field(fmt::format("{}:max={}:n={}\n", name, maxValue, nData)); }

void Stats_max::sample(const double v, bool en) {
  if (!en) {
    return;
  }
  maxValue = v > maxValue ? v : maxValue;
  nData++;
}

void Stats_max::reset() {
  maxValue = 0;
  nData    = 0;
}

/*********************** Stats_hist */

Stats_hist::Stats_hist(const std::string &str) : Stats(str), numSample(0), cumulative(0) {
  numSample  = 0;
  cumulative = 0;

  subscribe();
}

void Stats_hist::report() const {
  int32_t maxKey = 0;

  for (const auto &e : hist) {
    Report::field(fmt::format("{}({})={}\n", name, e.first, e.second));
    if (e.first > maxKey) {
      maxKey = e.first;
    }
  }
  long double div = cumulative;  // cummulative has 64bits (double has 54bits mantisa)
  div /= numSample;

  Report::field(fmt::format("{}:max={}\n", name, maxKey));
  Report::field(fmt::format("{}:v={}\n", name, div));
  Report::field(fmt::format("{}:n={}\n", name, numSample));
}

void Stats_hist::sample(bool enable, int32_t key, double weight) {
  if (enable) {
    if (hist.find(key) == hist.end()) {
      hist[key] = 0;
    }

    hist[key] += weight;

    numSample += weight;
    cumulative += weight * key;
  }
}

void Stats_hist::reset() {
  hist.clear();

  numSample  = 0;
  cumulative = 0;
}
