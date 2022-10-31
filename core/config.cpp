// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "fmt/format.h"

#include "config.hpp"

void Config::init(const std::string f) {

  filename = f;

  const char *e = getenv("DESESCCONF");
  if(e)
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

  auto sec = toml::find(data,block);
  if (!sec.contains(name)) {
    errors.emplace_back(fmt::format("section:{} does not have field named {} in configuration:{}\n", block, name, filename));
    return false;
  }

  return true;
}

std::string Config::get_string(const std::string &block, const std::string &name, const std::vector<std::string> allowed) {

  if (!check(block,name)) {
    return "INVALID";
  }

  {
    std::string env_var= fmt::format("DESESC_{}_{}",block,name);

    const char *e = getenv(env_var.c_str());
    if(e)
      return e;
  }

  auto ent = toml::find(data, block, name);
  if (!ent.is_string()) {
    errors.emplace_back(fmt::format("conf:{} section:{} field:{} is not a string\n", filename, block, name));
    return "INVALID";
  }

  std::string val{ent.as_string()};
  if (!allowed.empty()) {
    for(auto e:allowed) {
      auto same = std::equal(e.cbegin(), e.cend(), val.cbegin(), val.cend()
                             ,[](auto c1, auto c2) { return std::toupper(c1) == std::toupper(c2); }
                             );
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

  if (!check(block,name)) {
    return 0;
  }

  int val = 0;

  {
    std::string env_var= fmt::format("DESESC_{}_{}",block,name);

    const char *e = getenv(env_var.c_str());
    if(e) {
      val = std::atoi(e);
    }
  }

  auto ent = toml::find(data, block, name);
  if (!ent.is_integer()) {
    errors.emplace_back(fmt::format("conf:{} section:{} field:{} is not a integer\n", filename, block, name));
    return 0;
  }

  val = ent.as_integer();

  if (val<from || val>to) {
    errors.emplace_back(fmt::format("conf:{} section:{} field:{} value:{} is not allowed range ({}..<={})\n", filename, block, name, val, from, to));
    return 0;
  }

  return val;
}

size_t Config::get_array_size(const std::string &block, const std::string &name) {

  if (!check(block,name)) {
    return 0;
  }

  auto ent = toml::find(data, block, name);
  if (!ent.is_array()) {
    errors.emplace_back(fmt::format("conf:{} section:{} field:{} is not an array\n", filename, block, name));
    return 0;
  }

  return ent.as_array().size();
}

