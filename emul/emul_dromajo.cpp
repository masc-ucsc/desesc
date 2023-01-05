// See LICENSE for details.

#include "emul_dromajo.hpp"

#include "absl/strings/str_split.h"

#include <unistd.h>
#include <assert.h>
#include <stdio.h>

Emul_dromajo::Emul_dromajo() : Emul_base() {
  num = 0;

  auto nemuls = Config::get_array_size("soc", "emul");
  for (auto i = 0u; i < nemuls; ++i) {
    auto tp = Config::get_string("soc", "emul", i, "type");
    if (tp != "dromajo") {
      continue;
    }

    if (num == 0) {
      section = Config::get_string("soc", "emul", i);

      rabbit = Config::get_integer(section, "rabbit");
      detail = Config::get_integer(section, "detail");
      time   = Config::get_integer(section, "time");
      bench  = Config::get_string(section, "bench");
    }
    ++num;
  }
  std::vector<std::string> bench_split = absl::StrSplit(bench, ' ');
  if (bench_split.empty() || access(bench_split[0].c_str(), F_OK) == -1) {
    Config::add_error(fmt::format("could not open dromajo bench={}\n", bench));
    if (access(bench_split[0].c_str(), F_OK) == -1) {
      Config::add_error(fmt::format("file {} not accessible from {} path\n", bench, get_current_dir_name()));
    }
  }
  Config::exit_on_error();
  type = "dromajo";
  if (num) {
    init_dromajo_machine();
  }
}

Emul_dromajo::~Emul_dromajo() {}

void Emul_dromajo::destroy_machine() {
  if (machine != NULL) {
    virt_machine_end(machine);
  }
  // XXX - dromajo has a memory leak, needs to be fixed on that end
}

Dinst *Emul_dromajo::peek(Hartid_t fid) {
  uint64_t last_pc  = virt_machine_get_pc(machine, fid);
  uint32_t insn_raw = -1;
  (void)riscv_read_insn(machine->cpu_state[fid], &insn_raw, last_pc);
  Instruction esesc_insn;

  Opcode   opcode  = iAALU;  // dromajo wont pass invalid insns, default to ALU
  RegType  src1    = LREG_R0;
  RegType  src2    = LREG_R0;
  RegType  dst1    = LREG_InvalidOutput;
  RegType  dst2    = LREG_InvalidOutput;
  Addr_t   address = 0;
  uint32_t funct7  = (insn_raw >> 25) & 0x7F;
  uint32_t funct3  = (insn_raw >> 12) & 0x7;
  uint32_t rs1     = (insn_raw >> 15) & 0x1F;
  uint32_t rs2     = (insn_raw >> 20) & 0x1F;
  uint32_t rd      = (insn_raw >> 7) & 0x1F;
  switch (insn_raw & 0x3) {  // compressed
    case 0x0:                // C0
      break;
    case 0x1: break;
    case 0x2:
      rs1 = (insn_raw >> 7) & 0x1F;
      if (!(insn_raw & 0x7C) && rs1) {
        src1   = (RegType)(rs1);
        opcode = iBALU_RJUMP;
        address += virt_machine_get_reg(machine, fid, rs1);

        if (src1 == LREG_R1) {
          opcode = iBALU_RET;
        }
      }
      break;
    default:
      // TO-DO: the rest of floating point insns
      switch (insn_raw & 0x7F) {
        case 0x03:
          if (funct3 < 6) {
            opcode  = iLALU_LD;
            src1    = (RegType)(rs1);
            dst1    = (RegType)(rd);
            address = insn_raw >> 20;
            if (address & 0x800) {  // sign extend
              address |= 0xFFFFFFFFFFFFF000ull;
            }
            address += virt_machine_get_reg(machine, fid, rs1);
          }
          break;
        case 0x0F: opcode = iRALU; break;
        case 0x13:
          opcode = iAALU;
          src1   = (RegType)(rs1);
          dst1   = (RegType)(rd);
          break;
        case 0x23:
          if (funct3 < 4) {
            opcode  = iSALU_ST;
            src1    = (RegType)(rs1);
            src2    = (RegType)(rs2);
            address = (funct7 << 5) | rd;
            if (address & 0x800) {  // sign extend
              address |= 0xFFFFFFFFFFFFF000ull;
            }
            address += virt_machine_get_reg(machine, fid, rs1);
          }
          break;
        case 0x2F:
          opcode = iRALU;
          src1   = (RegType)(rs1);
          dst1   = (RegType)(rd);
          if (rs2) {
            src2 = (RegType)(rs2);
          }
          break;
        case 0x33:
          if (funct7 == 1) {
            if (funct3 < 4) {
              opcode = iCALU_MULT;
            } else {
              opcode = iCALU_DIV;
            }
          } else {
            opcode = iAALU;
          }

          src1 = (RegType)(rs1);
          src2 = (RegType)(rs2);
          dst1 = (RegType)(rd);
          break;
        case 0x3b:
          if (funct7 == 1) {
            if (funct3 == 0) {
              opcode = iCALU_MULT;
            } else if (funct3 > 3) {
              opcode = iCALU_DIV;
            }
          } else {
            opcode = iAALU;
          }

          src1 = (RegType)(rs1);
          src2 = (RegType)(rs2);
          dst1 = (RegType)(rd);
          break;
        case 0x63:
          opcode  = iBALU_LBRANCH;
          address = ((funct7 & 0x40) << 6) | ((rd & 1) << 11) | ((funct7 & 0x3F) << 5) | (rd & 0x1E);
          if (address & 0x1000) {  // sign extend
            address |= 0xFFFFFFFFFFFFE000ull;
          }
          address += last_pc;

          src1 = (RegType)(rs1);
          src2 = (RegType)(rs2);
          break;
        case 0x67:
          if (funct3 == 0) {
            opcode  = iBALU_RJUMP;
            src1    = (RegType)(rs1);
            dst1    = (RegType)(rd);
            address = (funct7 << 5) | rs2;
            if (address & 0x800) {  // sign extend
              address |= 0xFFFFFFFFFFFFF000ull;
            }

            if (dst1 == LREG_R0 && src1 == LREG_R1 && address == 0) {
              opcode = iBALU_RET;
            } else if (dst1 == LREG_R1) {
              opcode = iBALU_RCALL;
            }

            address += virt_machine_get_reg(machine, fid, rs1);
          }
          break;
        case 0x6F:
          opcode = iBALU_LJUMP;
          dst1   = (RegType)(rd);
          address
              = ((funct7 & 0x40) << 14) | (rs1 << 15) | (funct3 << 12) | ((rs2 & 1) << 11) | ((funct7 & 0x3F) << 5) | (rs2 & 0x1E);
          if (address & 0x100000) {  // sign extend
            address |= 0xFFFFFFFFFFE00000ull;
          }
          address += last_pc;

          if (dst1 == LREG_R1) {
            opcode = iBALU_LCALL;
          }
          break;
        case 0x73:
          opcode = iRALU;
          if (funct3 && funct3 != 0x4) {
            if (funct3 < 4) {
              src1 = (RegType)(rs1);
              dst1 = (RegType)(rd);
            }
          }
          break;
        default: break;
      }
  }

  // assert(dst1 != LREG_R0);

  esesc_insn.set(opcode, src1, src2, dst1, dst2);
  return Dinst::create(&esesc_insn, last_pc, address, fid, time);
}

void Emul_dromajo::execute(Hartid_t fid) {
  // no trace generated, only instruction executed
  virt_machine_run(machine, fid);
}

Hartid_t Emul_dromajo::get_num() const { return num; }

bool Emul_dromajo::is_sleeping(Hartid_t fid) const {
  fmt::print("called is_sleeping on hartid {}\n", fid);
  return true;
}

void Emul_dromajo::init_dromajo_machine() {
  assert(type == "dromajo");

  std::vector<const char *> dromajo_args;
  dromajo_args.push_back("desesc_drom");
  dromajo_args.push_back(bench.c_str());

  std::vector<std::string> list_args = {"cmdline",
                                        "ncpus",
                                        "load",
                                        "simpoint",
                                        "save",
                                        "maxinsns",
                                        "terminate-event",
                                        "trace",
                                        "ignore_sbi_shutdown",
                                        "dump_memories",
                                        "memory_size",
                                        "memory_addr",
                                        "bootrom",
                                        "dtb, compact_bootrom",
                                        "reset_vector",
                                        "plic",
                                        "clint",
                                        "custom_extension",
                                        "gdbinit",
                                        "clear_ids"};

  dromajo_args.reserve(list_args.size());

  for (auto &&item : list_args) {
    if (Config::has_entry(section, item)) {
      std::string arg = "--" + item + "=" + Config::get_string(section, item);
      dromajo_args.push_back(arg.c_str());
    }
  }
  char *argv[dromajo_args.size()];
  for (auto i = 0u; i < dromajo_args.size(); ++i) {
    argv[i] = const_cast<char *>(dromajo_args[i]);
  }

  machine = virt_machine_main(dromajo_args.size(), argv);
}

// TO-DO: Config::init(dromajo.cfg) ... as started below:
// auto bootroom  = Config::get_string("emul", "bootroom");
