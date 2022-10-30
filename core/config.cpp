/*
   ESESC: Super ESCalar simulator
   Copyright (C) 2003 University of Illinois.
   Copyright (C) 2009 University of California, Santa Cruz.

   Contributed by Jose Renau
                  Basilio Fraguela
                  Luis Ceze
                  Smruti Sarangi
                  Paul Sack

This file is part of ESESC.

ESESC is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License as published by the Free Software Foundation;
either version 2, or (at your option) any later version.

ESESC is    distributed in the  hope that  it will  be  useful, but  WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should  have received a copy of  the GNU General  Public License along with
ESESC; see the file COPYING.  If not, write to the  Free Software Foundation, 59
Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/


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

std::string Config::get_string(const std::string &block, const std::string &name) {

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

  return ent.as_string();
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

