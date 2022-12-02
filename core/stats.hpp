// See LICENSE for details.

#pragma once

#include <list>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "fmt/format.h"
#include "iassert.hpp"

class Stats {
private:
  static inline absl::flat_hash_map<std::string, Stats *> store;

protected:
  const std::string name;

  void subscribe();
  void unsubscribe();

public:
  Stats(const std::string n) : name(n){};
  virtual ~Stats();

  static void report(const std::string &str);
  static void reset_all();

  virtual void report() const = 0;
  virtual void reset()        = 0;
};

class Stats_cntr : public Stats {
private:
  double data;

protected:
public:
  Stats_cntr(const std::string &format);

  Stats_cntr &operator+=(const double v) {
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
  Stats_avg(const std::string &format);

  void sample(const double v, bool en);

  void report() const final;
  void reset() final;
};

class Stats_max : public Stats {
private:
protected:
  double  maxValue;
  int64_t nData;

public:
  Stats_max(const std::string &format);

  void sample(const double v, bool en);

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
  Stats_hist(const std::string &format);

  void sample(bool enable, int32_t key, double weight = 1);

  void report() const final;
  void reset() final;
};
