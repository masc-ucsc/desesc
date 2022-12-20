// See LICENSE for details.

#pragma once

#include <limits>
#include <vector>

#include "absl/container/flat_hash_map.h"

#include "toml.hpp"

class Config {
private:
  static inline std::string filename;
  static inline toml::value data;

  static inline std::vector<std::string> errors;
  static inline absl::flat_hash_map<std::string, std::pair<std::string,std::vector<std::string>> > used;

  static bool check(const std::string &block, const std::string &name);

  static int check_power2(const std::string &block, const std::string &name, int power2);

  static void add_used(const std::string &block, const std::string &name, size_t pos, const std::string &val);

  static std::string get_block2(const std::string &block, const std::string &name, size_t pos);

protected:
public:
  Config() = delete; // No object instance. All methods are static

  static void exit_on_error();

  static void init(const std::string f = "desesc.conf");

  static std::string get_string(const std::string &block, const std::string &name,
                                const std::vector<std::string> allowed = std::vector<std::string>());
  static std::string get_string(const std::string &block, const std::string &name, size_t pos,
                                const std::vector<std::string> allowed = std::vector<std::string>());
  static std::string get_string(const std::string &block, const std::string &name, size_t pos, const std::string &name2,
                                const std::vector<std::string> allowed = std::vector<std::string>());

  static int get_integer(const std::string &block, const std::string &name, int from = std::numeric_limits<int>::min(),
                         int to = std::numeric_limits<int>::max());
  static int get_integer(const std::string &block, const std::string &name, size_t pos, const std::string &name2,
                         int from = std::numeric_limits<int>::min(), int to = std::numeric_limits<int>::max());

  static size_t      get_array_size(const std::string &block, const std::string &name, size_t max_size = 1024);
  static int         get_array_integer(const std::string &block, const std::string &name, size_t pos);
  static std::string get_array_string(const std::string &block, const std::string &name, size_t pos);

  static void add_error(const std::string &err);

  static bool has_entry(const std::string &block, const std::string &field);
  static bool has_entry(const std::string &block, const std::string &name, size_t pos, const std::string &name2);

  static bool get_bool(const std::string &block, const std::string &name);
  static bool get_bool(const std::string &block, const std::string &name, size_t pos, const std::string &name2);

  static int get_power2(const std::string &block, const std::string &name, int from = std::numeric_limits<int>::min(),
                        int to = std::numeric_limits<int>::max());
  static int get_power2(const std::string &block, const std::string &name, size_t pos, const std::string &name2,
                        int from = std::numeric_limits<int>::min(), int to = std::numeric_limits<int>::max());

  static bool has_errors() { return !errors.empty(); }
  static void dump(int fd);
};
