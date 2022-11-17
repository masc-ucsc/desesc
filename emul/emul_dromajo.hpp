// See LICENSE for details.

#pragma once

#include "emul_base.hpp"
#include "dromajo.h"

class Emul_dromajo : public Emul_base {
protected:
  RISCVMachine *machine = NULL;
public:
  Emul_dromajo(Config conf);
  virtual ~Emul_dromajo();

  void destroy_machine();
  bool init_dromajo_machine(int argc, char **argv) {
    machine = virt_machine_main(argc, argv);
    if (machine == NULL)
      return false;
    return true;
  }

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
