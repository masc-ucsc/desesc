// See LICENSE for details.

#include "stats_code.hpp"

#include "report.hpp"

Stats_code::Stats_code(const std::string& str) : Stats(str) {
  reset();
  subscribe();
}

void Stats_code::report() const {
  for (const auto& it : prof) {
    const ProfEntry e = it.second;

    Report::field(
        fmt::format("{}_{}:n={}:cpi={}:wt={}:et={}:flush={}:prefetch={}:ldbr={}:bp1_hit={}:bp1_miss={}:bp2_hit={}:bp2_miss={}:"
                    "bp3_hit={}:bp3_miss={}:bp_hit2_miss3={}:bp_hit3_miss2={}:no_tl={}:on_time_tl={}:late_tl={}",
                    name,
                    it.first,
                    e.n / nTotal,
                    e.sum_cpi / e.n,
                    e.sum_wt / e.n,
                    e.sum_et / e.n,
                    e.sum_flush,
                    e.sum_prefetch,
                    e.ldbr,
                    e.sum_bp1_hit,
                    e.sum_bp1_miss,
                    e.sum_bp2_hit,
                    e.sum_bp2_miss,
                    e.sum_bp3_hit,
                    e.sum_bp3_miss,
                    e.sum_hit2_miss3,
                    e.sum_hit3_miss2,
                    e.sum_no_tl,
                    e.sum_on_time_tl,
                    e.sum_late_tl));
  }
}

void Stats_code::reset() {
  last_nCommitted = 0;
  last_clockTicks = 0;
  nTotal          = 0;

  prof.clear();
}

void Stats_code::sample(const uint64_t pc, const double nCommitted, const double clockTicks, double wt, double et, bool flush,
                        bool prefetch, int ldbr, bool bp1_miss, bool bp2_miss, bool bp3_miss, bool bp1_hit, bool bp2_hit,
                        bool bp3_hit, bool hit2_miss3, bool hit3_miss2, bool tl1_pred, bool tl1_unpred, bool tl2_pred,
                        bool tl2_unpred, int trig_ld_status) {
  double delta_nCommitted = nCommitted - last_nCommitted;
  double delta_clockTicks = clockTicks - last_clockTicks;

  last_nCommitted = nCommitted;
  last_clockTicks = clockTicks;

  if (delta_nCommitted == 0) {
    return;
  }

  double cpi = delta_clockTicks / delta_nCommitted;

  nTotal++;

  auto it = prof.find(pc);

  if (it == prof.end()) {
    ProfEntry e;
    e.n         = 1;
    e.sum_cpi   = cpi;
    e.sum_wt    = wt;
    e.sum_et    = et;
    e.sum_flush = flush ? 1 : 0;
    if (ldbr > 0) {
      e.ldbr = ldbr;
    }
    e.sum_bp1_hit  = bp1_hit ? 1 : 0;
    e.sum_bp1_miss = bp1_miss ? 1 : 0;
    e.sum_bp2_hit  = bp2_hit ? 1 : 0;
    e.sum_bp2_miss = bp2_miss ? 1 : 0;
    if (bp2_miss) {
      e.sum_trig_ld1_pred   = tl1_pred ? 1 : 0;
      e.sum_trig_ld1_unpred = tl1_unpred ? 1 : 0;
      e.sum_trig_ld2_pred   = tl2_pred ? 1 : 0;
      e.sum_trig_ld2_unpred = tl2_unpred ? 1 : 0;
    }
    e.sum_bp3_hit    = bp3_hit ? 1 : 0;
    e.sum_bp3_miss   = bp3_miss ? 1 : 0;
    e.sum_hit2_miss3 = hit2_miss3 ? 1 : 0;
    e.sum_hit3_miss2 = hit3_miss2 ? 1 : 0;
    e.sum_no_tl      = (trig_ld_status == -1) ? 1 : 0;
    e.sum_late_tl    = (trig_ld_status > 0) ? 1 : 0;
    e.sum_on_time_tl = (trig_ld_status == 0) ? 1 : 0;
    e.sum_prefetch   = prefetch ? 1 : 0;
    prof[pc]         = e;
  } else {
    it->second.sum_cpi += cpi;
    it->second.sum_wt += wt;
    it->second.sum_et += et;
    it->second.sum_flush += flush ? 1 : 0;
    if (ldbr > 0) {
      it->second.ldbr = ldbr;
    }
    it->second.sum_bp1_hit += bp1_hit ? 1 : 0;
    it->second.sum_bp1_miss += bp1_miss ? 1 : 0;
    it->second.sum_bp2_hit += bp2_hit ? 1 : 0;
    it->second.sum_bp2_miss += bp2_miss ? 1 : 0;
    it->second.sum_bp3_hit += bp3_hit ? 1 : 0;
    it->second.sum_bp3_miss += bp3_miss ? 1 : 0;
    it->second.sum_hit2_miss3 += hit2_miss3 ? 1 : 0;
    it->second.sum_hit3_miss2 += hit3_miss2 ? 1 : 0;
    it->second.sum_no_tl += (trig_ld_status == -1) ? 1 : 0;
    it->second.sum_late_tl += (trig_ld_status > 0) ? 1 : 0;
    it->second.sum_on_time_tl += (trig_ld_status == 0) ? 1 : 0;
    if (bp2_miss) {
      it->second.sum_trig_ld1_pred += tl1_pred ? 1 : 0;
      it->second.sum_trig_ld1_unpred += tl1_unpred ? 1 : 0;
      it->second.sum_trig_ld2_pred += tl2_pred ? 1 : 0;
      it->second.sum_trig_ld2_unpred += tl2_unpred ? 1 : 0;
    }
    it->second.sum_prefetch += prefetch ? 1 : 0;
    it->second.n++;
  }
}
