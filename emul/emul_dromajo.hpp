// See LICENSE for details.

#pragma once

#include "dromajo.h"
#include "emul_base.hpp"

class Emul_dromajo : public Emul_base {
protected:
  RISCVMachine *machine = nullptr;

  uint64_t    num;
  uint64_t    rabbit;
  uint64_t    detail;
  uint64_t    time;

  std::string bench;

  void init_dromajo_machine();
public:
  Emul_dromajo();
  virtual ~Emul_dromajo();

  void destroy_machine();

  virtual Dinst *peek(Hartid_t fid) final;
  virtual void   execute(Hartid_t fid) final;

  virtual Hartid_t get_num() const final;
  virtual bool     is_sleeping(Hartid_t fid) const;

  virtual void skip_rabbit(Hartid_t fid, size_t ninst) final {
    // TODO: do this more efficiently
    for (size_t i = 0; i < ninst; ++i) {
      execute(fid);
    }
  };
};
