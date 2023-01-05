// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "config.hpp"

#include <unistd.h>

#include "fmt/format.h"

void Config::init(const std::string f) {
  filename = f;

  const char *e = getenv("DESESCCONF");
  if (e) {
    filename = e;
  }

  if (access(filename.c_str(), F_OK) == -1) {
    errors.emplace_back(fmt::format("could not open configuration file named {}", filename));
    exit_on_error();
  }

  data = toml::parse(filename);
}

void Config::exit_on_error() {
  if (errors.empty()) {
    return;
  }

  for (const auto &e : errors) {
    fmt::print("ERROR:{}\n", e);
  }

  abort(); // Abort no exit to avoid the likely seg-faults of a bad configuration
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
    if (e) {
      std::string v{e};

      std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return std::tolower(c); });
      return v;
    }
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
        std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return std::tolower(c); });
        return e;
      }
    }

    errors.emplace_back(fmt::format("conf:{} section:{} field:{} value:{} is not allowed\n", filename, block, name, val));
    return "INVALID";
  }

  std::transform(val.begin(), val.end(), val.begin(), [](unsigned char c) { return std::tolower(c); });

  add_used(block, name, 0, val);

  return val;
}

std::string Config::get_string(const std::string &block, const std::string &name, size_t pos,
                               const std::vector<std::string> allowed) {
  auto val = get_block2(block, name, pos);

  if (!allowed.empty()) {
    for (auto e : allowed) {
      auto same = std::equal(e.cbegin(), e.cend(), val.cbegin(), val.cend(), [](auto c1, auto c2) {
        return std::toupper(c1) == std::toupper(c2);
      });
      if (same) {
        std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return std::tolower(c); });
        return e;
      }
    }

    errors.emplace_back(fmt::format("conf:{} section:{} field:{} value:{} is not allowed\n", filename, block, name, val));
    return "INVALID";
  }

  std::transform(val.begin(), val.end(), val.begin(), [](unsigned char c) { return std::tolower(c); });

  add_used(block, name, pos, val);

  return val;
}

std::string Config::get_string(const std::string &block, const std::string &name, size_t pos, const std::string &name2,
                               const std::vector<std::string> allowed) {
  auto block2 = get_block2(block, name, pos);

  {
    std::string env_var = fmt::format("DESESC_{}_{}", block2, name2);

    const char *e = getenv(env_var.c_str());
    if (e) {
      std::string v{e};

      std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return std::tolower(c); });
      return v;
    }
  }

  auto ent = toml::find(data, block2, name2);
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
        std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return std::tolower(c); });
        return e;
      }
    }

    errors.emplace_back(fmt::format("conf:{} section:{} field:{} value:{} is not allowed\n", filename, block, name, val));
    return "INVALID";
  }

  std::transform(val.begin(), val.end(), val.begin(), [](unsigned char c) { return std::tolower(c); });

  add_used(block2, name2, pos, val);

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
  if (!ent.is_integer() && !ent.is_floating()) {
    errors.emplace_back(fmt::format("conf:{} section:{} field:{} is not a integer\n", filename, block, name));
    return 0;
  }

  if (ent.is_integer())
    val = ent.as_integer();
  else
    val = ent.as_floating();

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

  add_used(block, name, 0, fmt::format("{}", val));

  return val;
}

int Config::get_integer(const std::string &block, const std::string &name, size_t pos, const std::string &name2, int from, int to) {
  auto block2 = get_block2(block, name, pos);
  if (block2.empty()) {
    return 0;
  }

  int val = 0;
  {
    std::string env_var = fmt::format("DESESC_{}_{}", block2, name2);

    const char *e = getenv(env_var.c_str());
    if (e) {
      val = std::atoi(e);
    }
  }

  auto ent2 = toml::find(data, block2, name2);
  if (!ent2.is_integer() && !ent2.is_floating()) {
    errors.emplace_back(fmt::format("conf:{} section:{} field:{} is not a integer\n", filename, block2, name2));
    return 0;
  }

  if (ent2.is_integer())
    val = ent2.as_integer();
  else
    val = ent2.as_floating();

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

  add_used(block2, name2, pos, fmt::format("{}", val));

  return val;
}

size_t Config::get_array_size(const std::string &block, const std::string &name, size_t max_size) {
  if (!check(block, name)) {
    return 0;
  }

  auto ent = toml::find(data, block, name);
  if (!ent.is_array()) {
    errors.emplace_back(fmt::format("conf:{} section:{} field:{} is not an array\n", filename, block, name));
    return 0;
  }

  auto i = ent.as_array().size();
  if (i > max_size) {
    errors.emplace_back(fmt::format("conf:{} section:{} field:{} has too many entries\n", filename, block, name));
    return max_size;
  }

  return i;
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

  if (!arr[pos].is_integer() && !arr[pos].is_floating()) {
    errors.emplace_back(fmt::format("conf:{} section:{} array entry is not integer\n", filename, block, name));
    return 0;
  }

  int val;
  if (arr[pos].is_integer())
    val = arr[pos].as_integer();
  else
    val = arr[pos].as_floating();

  add_used(block, name, pos, fmt::format("{}", val));
  return val;
}

std::string Config::get_array_string(const std::string &block, const std::string &name, size_t pos) {
  if (!check(block, name)) {
    return "INVALID";
  }

  auto ent = toml::find(data, block, name);
  if (!ent.is_array()) {
    errors.emplace_back(fmt::format("conf:{} section:{} field:{} is not an array\n", filename, block, name));
    return "INVALID";
  }

  if (ent.as_array().size() <= pos) {
    errors.emplace_back(
        fmt::format("conf:{} section:{} out of bounds {} array of size {}\n", filename, block, name, ent.as_array().size(), pos));
    return "INVALID";
  }

  auto arr = ent.as_array();

  if (!arr[pos].is_string()) {
    errors.emplace_back(fmt::format("conf:{} section:{} array entry is not string\n", filename, block, name));
    return "INVALID";
  }

  std::string val = arr[pos].as_string();
  std::transform(val.begin(), val.end(), val.begin(), [](unsigned char c) { return std::tolower(c); });

  add_used(block, name, pos, val);

  return val;
}

void Config::add_error(const std::string &err) { errors.emplace_back(err); }

bool Config::has_entry(const std::string &block, const std::string &name) {
  if (block.empty()) {
    return false;
  }

  if (!data.contains(block)) {
    return false;
  }

  auto sec = toml::find(data, block);
  return sec.contains(name);
}

bool Config::has_entry(const std::string &block, const std::string &name, size_t pos, const std::string &name2) {
  if (!has_entry(block, name)) {
    return false;
  }

  auto ent = toml::find(data, block, name);
  if (!ent.is_array()) {
    return false;
  }

  auto ent_array = ent.as_array();
  if (ent_array.size() <= pos) {
    return false;
  }

  const auto t_block2 = ent_array[pos];
  if (!t_block2.is_string()) {
    return false;
  }

  auto ent2 = toml::find(data, t_block2.as_string());

  return ent2.contains(name2);
}

bool Config::get_bool(const std::string &block, const std::string &name) {
  if (!check(block, name)) {
    return false;
  }

  {
    std::string env_var = fmt::format("DESESC_{}_{}", block, name);

    const char *e = getenv(env_var.c_str());
    if (e) {
      return strcasecmp(e, "true") == 0;
    }
  }

  auto ent = toml::find(data, block, name);
  if (!ent.is_boolean()) {
    errors.emplace_back(fmt::format("conf:{} section:{} field:{} is not a boolean\n", filename, block, name));
    return false;
  }

  auto val = ent.as_boolean();

  add_used(block, name, 0, val ? "true" : "false");

  return val;
}

bool Config::get_bool(const std::string &block, const std::string &name, size_t pos, const std::string &name2) {
  if (!check(block, name)) {
    return false;
  }

  auto ent = toml::find(data, block, name);
  if (!ent.is_array()) {
    errors.emplace_back(
        fmt::format("conf:{} section:{} field:{} is not a array needed to chain to {}\n", filename, block, name, name2));
    return false;
  }

  auto ent_array = ent.as_array();

  if (ent_array.size() <= pos) {
    errors.emplace_back(fmt::format("conf:{} section:{} field:{} out-of-bounds array access {}\n", filename, block, name, pos));
    return false;
  }

  const auto t_block2 = ent_array[pos];
  if (!t_block2.is_string()) {
    errors.emplace_back(fmt::format("conf:{} section:{} field:{} should point to a section\n", filename, block, name));
    return false;
  }

  std::string block2{t_block2.as_string()};
  {
    std::string env_var = fmt::format("DESESC_{}_{}", block, name);

    const char *e = getenv(env_var.c_str());
    if (e) {
      return strcasecmp(e, "true") == 0;
    }
  }

  auto ent2 = toml::find(data, block2, name2);
  if (!ent2.is_boolean()) {
    errors.emplace_back(fmt::format("conf:{} section:{} field:{} is not a boolean\n", filename, block2, name2));
    return false;
  }

  auto val = ent2.as_boolean();

  add_used(block2, name2, pos, val ? "true" : "false");

  return val;
}

int Config::check_power2(const std::string &block, const std::string &name, int v) {
  if (v == 0) {
    return 0;
  }

  if (v < 0) {
    add_error(fmt::format("entry {} field {}={} is negative, not a power of two", block, name, v));
    return 0;
  }
  if ((v & (v - 1)) != 0) {
    add_error(fmt::format("entry {} field {}={} is not a power of two", block, name, v));
    return 0;
  }

  return v;
}

std::string Config::get_block2(const std::string &block, const std::string &name, size_t pos) {
  if (!check(block, name)) {
    return "";
  }

  auto ent = toml::find(data, block, name);
  if (!ent.is_array()) {
    errors.emplace_back(fmt::format("conf:{} section:{} field:{} is not a array needed to chain\n", filename, block, name));
    return "";
  }

  auto ent_array = ent.as_array();

  if (ent_array.size() <= pos) {
    errors.emplace_back(fmt::format("conf:{} section:{} field:{} out-of-bounds array access {}\n", filename, block, name, pos));
    return "";
  }

  const auto t_block2 = ent_array[pos];
  if (!t_block2.is_string()) {
    errors.emplace_back(fmt::format("conf:{} section:{} field:{} should point to a section\n", filename, block, name));
    return "";
  }

  auto val = t_block2.as_string();

  add_used(block, name, pos, val);

  return val;
}

void Config::add_used(const std::string &block, const std::string &name, size_t pos, const std::string &val) {
  if (used[block].second.size() <= pos) {
    used[block].second.resize(pos + 1);
    used[block].first = name;
  }
  used[block].second[pos] = val;
}

void Config::dump(int fd) {
  for (const auto &u : used) {
    auto str = fmt::format("[{}]\n", u.first);
    auto sz  = ::write(fd, str.c_str(), str.size());
    (void)sz;

    if (u.second.second.size() == 1) {
      str = fmt::format("{} = {}\n", u.second.first, u.second.second[0]);
    } else {
      str        = fmt::format("{} = {{ ", u.second.first);
      bool first = true;
      for (const auto &e : u.second.second) {
        if (first) {
          str += fmt::format("{}", e);
        } else {
          str += fmt::format(", {}", e);
        }
      }
      str += fmt::format(" }}\n");
    }
    sz = ::write(fd, str.c_str(), str.size());
    (void)sz;
  }
}

int Config::get_power2(const std::string &block, const std::string &name, int from, int to) {
  int v = get_integer(block, name, from, to);

  return check_power2(block, name, v);
}

int Config::get_power2(const std::string &block, const std::string &name, size_t pos, const std::string &name2, int from, int to) {
  int v = get_integer(block, name, pos, name2, from, to);

  return check_power2(block, name, v);
}
