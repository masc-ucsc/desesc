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

********************************************************************************/

#pragma once

#include <vector>

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

  static std::string get_string(const std::string &block, const std::string &name);

  static size_t get_array_size(const std::string &block, const std::string &name);

};


