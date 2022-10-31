// See LICENSE for details.

#pragma once

#include <vector>
#include <limits>

#include "toml.hpp"

class Config {
private:
  static inline std::string filename;
  static inline toml::value data;

  static inline std::vector<std::string> errors;

  static bool check(const std::string &block, const std::string &name);

protected:
public:
  static void init(const std::string f="desesc.conf");

  static std::string get_string(const std::string &block, const std::string &name, const std::vector<std::string> allowed = std::vector<std::string>());

  static int get_integer(const std::string &block, const std::string &name, int from=std::numeric_limits<int>::min(), int to=std::numeric_limits<int>::max());

  static size_t get_array_size(const std::string &block, const std::string &name);
};

