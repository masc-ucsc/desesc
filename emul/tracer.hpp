// See license for details

#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

#include "absl/container/flat_hash_set.h"

#include "dinst.hpp"

class Tracer {
public:

  static bool open(const std::string &fname);

  Tracer() {
    // disabled until open
    track_from = UINT64_MAX;
    track_to   = UINT64_MAX;
  }

  static void track_range(uint64_t from, uint64_t to=UINT64_MAX);

  static void stage(const Dinst *, const std::string ev);
  static void event(const Dinst *, const std::string ev);

  static void commit(const Dinst *);
  static void flush(const Dinst *);

  static void advance_clock() {
    if (likely(!ofs))
      return;

    last_clock = globalClock;
  }

  ~Tracer() {
    if (ofs)
      ofs.close();
  }
private:
  static void adjust_clock();

  static inline absl::flat_hash_set<uint64_t> started;
  static inline std::vector<std::string> pending_end;

  static inline bool   main_clock_set;
  static inline Time_t last_clock;

  static inline uint64_t track_from;
  static inline uint64_t track_to;
  static inline std::ofstream ofs;
};
