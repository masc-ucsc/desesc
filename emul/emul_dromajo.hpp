// See LICENSE for details.

#pragma once

#include "emul_base.hpp"

class Emul_dromajo : public Emul_base {
protected:

public:
  Emul_dromajo(const std::string &section);
  virtual ~Emul_dromajo();

  virtual Dinst *peek(Hartid_t fid) final;
  virtual void   execute(Hartid_t fid) final;

  virtual Hartid_t get_num() const final;

  virtual void skip_rabbit(Hartid_t fid, size_t ninst) final;
};

