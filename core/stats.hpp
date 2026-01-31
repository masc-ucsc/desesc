// See LICENSE for details.

#pragma once

#include <cstdlib>
#include <list>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "fmt/format.h"
#include "iassert.hpp"

class Stats {
private:
  static inline absl::flat_hash_map<std::string, Stats*> store;

protected:
  const std::string name;

  void subscribe();
  void unsubscribe();

public:
  Stats(const std::string& n) : name(n) {};
  virtual ~Stats();

  static void report_all();
  static void reset_all();

  virtual void report() const = 0;
  virtual void reset()        = 0;
};

class Stats_pwr : public Stats {
private:
  uint64_t cntr_tran{0};
  uint64_t cntr_real{0};

protected:
public:
  Stats_pwr(const std::string& format);

  void inc(bool transient) {
    cntr_tran += transient ? 1 : 0;
    cntr_real += transient ? 0 : 1;
  }

  void report() const final;
  void reset() final;
};

class Stats_cntr : public Stats {
private:
  double data;

protected:
public:
  Stats_cntr(const std::string& format);

  Stats_cntr& operator+=(const double v) {
    data += v;
    return *this;
  }

  void add(const double v, bool en = true) { data += en ? v : 0; }
  void inc(bool en = true) { data += en ? 1 : 0; }

  void dec(bool en) { data -= en ? 1 : 0; }

  void report() const final;
  void reset() final;
};

class Stats_avg : public Stats {
private:
protected:
  double  data;
  int64_t nData;

public:
  Stats_avg(const std::string& format);

  void sample(const double v, bool en);
  void sample(bool en, const double v) = delete;

  void report() const final;
  void reset() final;
};

class Stats_max : public Stats {
private:
protected:
  double  maxValue;
  int64_t nData;

public:
  Stats_max(const std::string& format);

  void sample(const double v, bool en);
  void sample(bool en, const double v) = delete;

  void report() const final;
  void reset() final;
};

class Stats_hist : public Stats {
private:
protected:
  double numSample;
  double cumulative;

  absl::flat_hash_map<int32_t, double> hist;

public:
  Stats_hist(const std::string& format);

  void sample(int32_t key, bool enable, double weight = 1);
  void sample(bool enable, uint32_t key, double weight = 1) = delete;

  void report() const final;
  void reset() final;
};
