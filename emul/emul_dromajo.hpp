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
  bool init_dromajo_machine(char *dromajo_cfg) {
    assert(type == "dromajo");
    assert(dromajo_cfg);

    std::vector<char*> dromajo_args;
    char dummy_arg[] = "arg0";
    dromajo_args.push_back(dummy_arg);
    dromajo_args.push_back(dromajo_cfg);


    std::vector<std::string> list_args = { "cmdline", "ncpus", "load", "simpoint", "save", "maxinsns", 
                                           "terminate-event", "trace", "ignore_sbi_shutdown",
                                           "dump_memories", "memory_size", "memory_addr", "bootrom",
                                           "dtb, compact_bootrom", "reset_vector", "plic", "clint",
                                           "custom_extension", "gdbinit", "clear_ids" };
    dromajo_args.reserve(list_args.size());
    for (auto&& item : list_args)
        if (Config::has_entry("drom_emu", item)) {
            std::string arg = "--" + item + "=" + Config::get_string("drom_emu", item);
            dromajo_args.push_back(const_cast<char*>(arg.c_str()));
        }

    machine = virt_machine_main(dromajo_args.size(), &dromajo_args[0]);
    return machine;
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
