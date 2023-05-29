// See LICENSE for details.

#pragma once

#include "dromajo.h"
#include "emul_base.hpp"

class Emul_dromajo : public Emul_base {
private:
  RISCVMachine *machine = nullptr;

  uint64_t num;
  uint64_t detail;
  uint64_t time;

  std::string bench;

  void init_dromajo_machine();

public:
  Emul_dromajo();
  Emul_dromajo(const Emul_dromajo &)            = default;
  Emul_dromajo(Emul_dromajo &&)                 = delete;
  Emul_dromajo &operator=(const Emul_dromajo &) = default;
  Emul_dromajo &operator=(Emul_dromajo &&)      = delete;
  ~Emul_dromajo() override                      = default;

  void destroy_machine();

  Dinst *peek(Hartid_t fid) final;
  void   execute(Hartid_t fid) final;

  [[nodiscard]] Hartid_t get_num() const final;
  [[nodiscard]] bool     is_sleeping(Hartid_t fid) const override;

  void skip_rabbit(Hartid_t fid, size_t ninst) final {
    // TODO(??): do this more efficiently
    for (size_t i = 0; i < ninst; ++i) {
      execute(fid);
    }
  }

  void set_detail(uint64_t ninst) { detail = ninst; }
  void set_time(uint64_t ninst) { time = ninst; }
};
