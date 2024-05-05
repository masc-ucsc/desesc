// See LICENSE for details.

#pragma once

#include <string>

#include "absl/container/flat_hash_map.h"
#include "stats.hpp"

class Stats_code : public Stats {
private:
  class ProfEntry {
  public:
    ProfEntry() = default;
    double   n{0};
    double   sum_cpi{0};
    double   sum_wt{0};
    double   sum_et{0};
    uint64_t sum_flush{0};
    int      ldbr{0};
    uint64_t sum_bp1_hit{0};
    uint64_t sum_bp2_hit{0};
    uint64_t sum_bp3_hit{0};
    uint64_t sum_bp1_miss{0};
    uint64_t sum_bp2_miss{0};
    uint64_t sum_bp3_miss{0};
    uint64_t sum_hit2_miss3{0};
    uint64_t sum_hit3_miss2{0};
    uint64_t sum_no_tl{0};
    uint64_t sum_late_tl{0};
    uint64_t sum_on_time_tl{0};
    uint64_t sum_trig_ld1_pred{0};
    uint64_t sum_trig_ld1_unpred{0};
    uint64_t sum_trig_ld2_pred{0};
    uint64_t sum_trig_ld2_unpred{0};
    uint64_t sum_prefetch{0};
  };

  absl::flat_hash_map<uint64_t, ProfEntry> prof;

  double last_nCommitted;
  double last_clockTicks;
  double nTotal;

protected:
public:
  Stats_code(const std::string &format);

  void sample(const uint64_t pc, const double nCommitted, const double clockTicks, double wt, double et, bool flush, bool prefetch,
              int ldbr = 0, bool bp1_miss = 0, bool bp2_miss = 0, bool bp3_miss = 0, bool bp1_hit = 0, bool bp2_hit = 0,
              bool bp3_hit = 0, bool hit2_miss3 = 0, bool hit3_miss2 = 0, bool tl1_pred = 0, bool tl1_unpred = 0, bool tl2_pred = 0,
              bool tl2_unpred = 0, int trig_ld_status = -1);

  void report() const final;
  void reset() final;
};
