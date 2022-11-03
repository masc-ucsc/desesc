// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "config.hpp"

#include "fmt/format.h"

void Config::init(const std::string f) {
  filename = f;

  const char *e = getenv("DESESCCONF");
  if (e)
    filename = e;

  data = toml::parse(filename);
}

bool Config::check(const std::string &block, const std::string &name) {
  if (block.empty()) {
    errors.emplace_back(fmt::format("section is empty for configuration:{}\n", filename));
    return false;
  }

  if (!data.contains(block)) {
    errors.emplace_back(fmt::format("section:{} does not exist in configuration:{}\n", block, filename));
    return false;
  }

  auto sec = toml::find(data, block);
  if (!sec.contains(name)) {
    errors.emplace_back(fmt::format("section:{} does not have field named {} in configuration:{}\n", block, name, filename));
    return false;
  }

  return true;
}

std::string Config::get_string(const std::string &block, const std::string &name, const std::vector<std::string> allowed) {
  if (!check(block, name)) {
    return "INVALID";
  }

  {
    std::string env_var = fmt::format("DESESC_{}_{}", block, name);

    const char *e = getenv(env_var.c_str());
    if (e)
      return e;
  }

  auto ent = toml::find(data, block, name);
  if (!ent.is_string()) {
    errors.emplace_back(fmt::format("conf:{} section:{} field:{} is not a string\n", filename, block, name));
    return "INVALID";
  }

  std::string val{ent.as_string()};
  if (!allowed.empty()) {
    for (auto e : allowed) {
      auto same = std::equal(e.cbegin(), e.cend(), val.cbegin(), val.cend(), [](auto c1, auto c2) {
        return std::toupper(c1) == std::toupper(c2);
      });
      if (same) {
        return e;
      }
    }

    errors.emplace_back(fmt::format("conf:{} section:{} field:{} value:{} is not allowed\n", filename, block, name, val));
    return "INVALID";
  }

  return val;
}

int Config::get_integer(const std::string &block, const std::string &name, int from, int to) {
  if (!check(block, name)) {
    return 0;
  }

  int val = 0;

  {
    std::string env_var = fmt::format("DESESC_{}_{}", block, name);

    const char *e = getenv(env_var.c_str());
    if (e) {
      val = std::atoi(e);
    }
  }

  auto ent = toml::find(data, block, name);
  if (!ent.is_integer()) {
    errors.emplace_back(fmt::format("conf:{} section:{} field:{} is not a integer\n", filename, block, name));
    return 0;
  }

  val = ent.as_integer();

  if (val < from || val > to) {
    errors.emplace_back(fmt::format("conf:{} section:{} field:{} value:{} is not allowed range ({}..<={})\n",
                                    filename,
                                    block,
                                    name,
                                    val,
                                    from,
                                    to));
    return 0;
  }

  return val;
}

int Config::get_integer(const std::string &block, const std::string &name, size_t pos, const std::string &name2, int from, int to) {
  if (!check(block, name)) {
    return 0;
  }

  auto ent = toml::find(data, block, name);
  if (!ent.is_array()) {
    errors.emplace_back(
        fmt::format("conf:{} section:{} field:{} is not a array needed to chain to {}\n", filename, block, name, name2));
    return 0;
  }

  auto ent_array = ent.as_array();

  if (ent_array.size() <= pos) {
    errors.emplace_back(fmt::format("conf:{} section:{} field:{} out-of-bounds array access {}\n", filename, block, name, pos));
    return 0;
  }

  const auto t_block2 = ent_array[pos];
  if (!t_block2.is_string()) {
    errors.emplace_back(fmt::format("conf:{} section:{} field:{} should point to a section\n", filename, block, name));
    return 0;
  }

  std::string block2{t_block2.as_string()};

  int val = 0;
  {
    std::string env_var = fmt::format("DESESC_{}_{}", block2, name2);

    const char *e = getenv(env_var.c_str());
    if (e) {
      val = std::atoi(e);
    }
  }

  auto ent2 = toml::find(data, block2, name2);
  if (!ent2.is_integer()) {
    errors.emplace_back(fmt::format("conf:{} section:{} field:{} is not a integer\n", filename, block2, name2));
    return 0;
  }

  val = ent2.as_integer();

  if (val < from || val > to) {
    errors.emplace_back(fmt::format("conf:{} section:{} field:{} value:{} is not allowed range ({}..<={})\n",
                                    filename,
                                    block2,
                                    name2,
                                    val,
                                    from,
                                    to));
    return 0;
  }

  return val;
}

size_t Config::get_array_size(const std::string &block, const std::string &name) {
  if (!check(block, name)) {
    return 0;
  }

  auto ent = toml::find(data, block, name);
  if (!ent.is_array()) {
    errors.emplace_back(fmt::format("conf:{} section:{} field:{} is not an array\n", filename, block, name));
    return 0;
  }

  return ent.as_array().size();
}

int Config::get_array_integer(const std::string &block, const std::string &name, size_t pos) {
  if (!check(block, name)) {
    return 0;
  }

  auto ent = toml::find(data, block, name);
  if (!ent.is_array()) {
    errors.emplace_back(fmt::format("conf:{} section:{} field:{} is not an array\n", filename, block, name));
    return 0;
  }

  if (ent.as_array().size() <= pos) {
    errors.emplace_back(
        fmt::format("conf:{} section:{} out of bounds {} array of size {}\n", filename, block, name, ent.as_array().size(), pos));
    return 0;
  }

  auto arr = ent.as_array();

  if (!ent.is_integer()) {
    errors.emplace_back(fmt::format("conf:{} section:{} array entry is not integer\n", filename, block, name));
    return 0;
  }

  return arr[pos].as_integer();
}

void Config::add_error(const std::string &err) { errors.emplace_back(err); }

bool Config::has_entry(const std::string &block, const std::string field) {
  if (block.empty()) {
    errors.emplace_back(fmt::format("section is empty for configuration:{}\n", filename));
    return false;
  }

  if (!data.contains(block)) {
    return false;
  }

  auto sec = toml::find(data, block);
  return sec.contains(name);
}

bool Config::get_bool(const std::string &block, const std::string &name) {
  auto v = get_string(block, name, {"true", "false"});

  return v == "true";
}

int Config::get_power2(const std::string &block, const std::string &name, int from, int to) {
  int v = get_integer(block, name, from, to);
  if (v == 0)
    return 0;

  if (v < 0) {
    add_error(fmt::format("entry {} field {}={} is negative, not a power of two", block, name, v));
    return 0;
  }
  if ((v) & (v - 1) != 0) {
    add_error(fmt::format("entry {} field {}={} is not a power of two", block, name, v));
    return 0;
  }

  return v;
}
