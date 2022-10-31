// See LICENSE for details.

#pragma once

#include <vector>
#include <string>

#include "dinst.hpp"
#include "iassert.hpp"


class Emul_base {
protected:
  std::string     section;
  std::string     type; // dromajo, trace,...
  int             num;

public:
  Emul_base(const std::string &section);
  virtual ~Emul_base();

  virtual Dinst *peek(Hartid_t fid)    = 0;
  virtual void   execute(Hartid_t fid) = 0;

  virtual Hartid_t get_num() const = 0;

  virtual void skip_rabbit(Hartid_t fid, size_t ninst) = 0;

  const std::string &get_type() const { return type; }
};

