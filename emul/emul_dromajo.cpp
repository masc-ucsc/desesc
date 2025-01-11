// See LICENSE for details.

#include "emul_dromajo.hpp"

#include <filesystem>

#include "absl/strings/str_split.h"

Emul_dromajo::Emul_dromajo() : Emul_base() {
  num = 0;

  uint64_t rabbit = 0;

  auto nemuls = Config::get_array_size("soc", "emul");

  last.resize(nemuls, {0, 0, 0, 0});

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
      if (Config::has_entry(section, "bench")) {
        bench = Config::get_string(section, "bench");
        if (Config::has_entry(section, "load")) {
          Config::add_error(fmt::format("section {} has both benchmark and load set. These are contradictory", section));
        }
      }
    }
    ++num;
  }
  if (!bench.empty()) {
    std::vector<std::string> bench_split = absl::StrSplit(bench, ' ');
    if (bench_split.empty() || !std::filesystem::exists(bench_split[0])) {
      Config::add_error(fmt::format("could not open dromajo bench={}\n", bench));
      if (!std::filesystem::exists(bench_split[0])) {
        std::string path{std::filesystem::current_path().generic_string()};
        Config::add_error(fmt::format("file {} not accessible from {} path\n", bench, path));
      }
    }
  }
  Config::exit_on_error();

  type = "dromajo";
  if (num) {
    init_dromajo_machine();
  }
  if (rabbit) {
    for (auto i = 0u; i < num; ++i) {
      skip_rabbit(i, rabbit);
    }
  } else {
    for (auto i = 0u; i < num; ++i) {
      execute(i);  // to set the last
    }
  }
}

void Emul_dromajo::destroy_machine() {
  if (machine != NULL) {
    virt_machine_end(machine);
  }
  // XXX - dromajo has a memory leak, needs to be fixed on that end
}

static inline uint32_t C_reg_decode(uint32_t rn) { return rn + 8; }

Dinst *Emul_dromajo::peek(Hartid_t fid) {
  uint32_t insn_raw = last[fid].insns;

  // Assume compressed, default to 32-bit insn
  uint32_t funct7 = 0;
  uint32_t rs1    = 0;
  uint32_t rs2    = 0;
  uint32_t rd     = 0;
  uint32_t funct3 = (insn_raw >> 13) & 0x7;  // funct3 has 3 bits
  Opcode   opcode = Opcode::iAALU;           // dromajo wont pass invalid insns, default to ALU
  RegType  src1   = RegType::LREG_INVALID;
  RegType  src2   = RegType::LREG_INVALID;
  RegType  dst1   = RegType::LREG_INVALID;
  RegType  dst2   = RegType::LREG_InvalidOutput;
  switch (insn_raw & 0x3) {  // compressed
    case 0x0:                // C0
      rs1  = C_reg_decode((insn_raw >> 7) & 0x7);
      rd   = C_reg_decode((insn_raw >> 2) & 0x7);
      src1 = (RegType)(rs1);

      if (funct3 == 1 || funct3 == 5) {  // FP LD/ST
        rd += 32;
      }

      if (funct3 >= 5) {
        opcode = Opcode::iSALU_ST;
        src2   = (RegType)(rd);
        dst1   = RegType::LREG_InvalidOutput;
      } else {
        opcode = Opcode::iLALU_LD;
        src2   = RegType::LREG_NoDependence;
        dst1   = (RegType)(rd);
      }
      break;
    case 0x1:  // C1
      src2 = RegType::LREG_NoDependence;

      if (insn_raw == 1) {  // NOP
        src1 = RegType::LREG_NoDependence;
        dst1 = RegType::LREG_InvalidOutput;
      } else if (funct3 == 5) {
        opcode = Opcode::iBALU_LJUMP;
        src1   = RegType::LREG_NoDependence;
        dst1   = RegType::LREG_InvalidOutput;
      } else if (funct3 < 4) {
        rs1  = (insn_raw >> 7) & 0x1F;
        src1 = (RegType)(rs1);
        dst1 = src1;
      } else {
        rs1    = C_reg_decode((insn_raw >> 7) & 0x7);
        src1   = (RegType)(rs1);
        funct3 = (insn_raw >> 10) & 0x3;

        if (funct3 == 4) {
          rd = static_cast<int>(src1);

          if (funct7 == 3) {
            rs2  = C_reg_decode((insn_raw >> 2) & 0x7);
            src2 = (RegType)(rs2);
          }
        } else {
          opcode = Opcode::iBALU_LBRANCH;
          dst1   = RegType::LREG_InvalidOutput;
        }
      }
      break;
    case 0x2:
      rd  = (insn_raw >> 7) & 0x1F;
      rs2 = (insn_raw >> 2) & 0x1F;

      if (funct3 == 1 || funct3 == 5) {
        rd += 32;  // FP LD/ST
        rs2 += 32;
      }

      if (insn_raw == 0x9002) {  // ebreak
        dst1 = RegType::LREG_InvalidOutput;
        src1 = src2 = RegType::LREG_NoDependence;
      } else if (funct3 < 4) {
        dst1 = (RegType)(rd);
        src2 = RegType::LREG_NoDependence;

        if (funct3 != 0) {
          src1   = (RegType)(2);
          opcode = Opcode::iLALU_LD;
        } else {
          src1 = (RegType)(rd);
        }
      } else if (funct3 > 4) {
        src2   = (RegType)(rs2);
        dst1   = RegType::LREG_InvalidOutput;
        src1   = (RegType)(2);
        opcode = Opcode::iSALU_ST;

      } else {
        funct7 = (insn_raw >> 12) & 0x1;
        rs1    = rd;
        src1   = (RegType)(rs1);

        if (funct7 == 0 && rs2 == 0) {  // C.JR
          src2   = RegType::LREG_NoDependence;
          dst1   = (RegType)(0);
          opcode = Opcode::iBALU_RJUMP;

          if (src1 == RegType::LREG_R1) {
            opcode = Opcode::iBALU_RET;
          }
        } else if (funct7 == 1 && rs2 == 0) {  // C.JALR
          src2   = RegType::LREG_NoDependence;
          dst1   = (RegType)(1);
          opcode = Opcode::iBALU_RJUMP;
        } else {
          if (funct7 == 0) {
            src1 = (RegType)(0);
          }
          src2 = (RegType)(rs2);
          dst1 = (RegType)(rd);
        }
      }
      break;
    default:
      funct7 = (insn_raw >> 25) & 0x7F;
      funct3 = (insn_raw >> 12) & 0x7;
      rs1    = (insn_raw >> 15) & 0x1F;
      rs2    = (insn_raw >> 20) & 0x1F;
      rd     = (insn_raw >> 7) & 0x1F;
      // TO-DO: the rest of floating point insns
      switch (insn_raw & 0x7F) {
        case 0x03:
          if (funct3 <= 6) {
            opcode = Opcode::iLALU_LD;
            src1   = (RegType)(rs1);
            src2   = RegType::LREG_NoDependence;
            dst1   = (RegType)(rs1);
          }
          break;
        case 0x07:  //   FP Load
          opcode = Opcode::iLALU_LD;
          src1   = (RegType)(rs1);
          src2   = RegType::LREG_NoDependence;
          dst1   = (RegType)(rd + 32);
          break;
        case 0x0F:
          opcode = Opcode::iRALU;  // XXX - fence and fence.i, is this right??
          src1   = (RegType)(rs1);
          src2   = RegType::LREG_NoDependence;
          dst1   = (RegType)(rd);
          break;
        case 0x13:
          src1 = (RegType)(rs1);
          src2 = RegType::LREG_NoDependence;
          dst1 = (RegType)(rd);
          break;
        case 0x17:
          src1 = RegType::LREG_NoDependence;
          src2 = RegType::LREG_NoDependence;
          dst1 = (RegType)(rd);
          break;
        case 0x1b:
          src1 = (RegType)(rs1);
          src2 = RegType::LREG_NoDependence;
          dst1 = (RegType)(rd);
          break;
        case 0x23:
          opcode = Opcode::iSALU_ST;
          src1   = (RegType)(rs1);
          src2   = (RegType)(rs2);
          dst1   = RegType::LREG_InvalidOutput;
          break;
        case 0x27:  // FP Store
          opcode = Opcode::iSALU_ST;
          src1   = (RegType)(rs1);
          src2   = (RegType)(rs2 + 32);
          dst1   = RegType::LREG_InvalidOutput;
          break;
        case 0x2F:
          opcode = Opcode::iRALU;
          src1   = (RegType)(rs1);
          src2   = (RegType)(rs2);
          dst1   = (RegType)(rd);
          if (!rs2) {
            src2 = RegType::LREG_NoDependence;
          }
          break;
        case 0x33:
          if (funct7 == 1) {
            if (funct3 < 4) {
              opcode = Opcode::iCALU_MULT;
            } else {
              opcode = Opcode::iCALU_DIV;
            }
          }
          src1 = (RegType)(rs1);
          src2 = (RegType)(rs2);
          dst1 = (RegType)(rd);
          break;
        case 0x37:  // LUI
          src1 = RegType::LREG_NoDependence;
          src2 = RegType::LREG_NoDependence;
          dst1 = (RegType)(rd);
          break;
        case 0x3b:
          if (funct7 == 1) {
            if (funct3 == 0) {
              opcode = Opcode::iCALU_MULT;
            } else if (funct3 > 3) {
              opcode = Opcode::iCALU_DIV;
            }
          }
          src1 = (RegType)(rs1);
          src2 = (RegType)(rs2);
          dst1 = (RegType)(rd);
          break;
        case 0x43:  // fmadd fd, fs1, fs2, fs3
        case 0x47:
        case 0x4b:
        case 0x4f:
          I(false);  // add support for R4 format (3 sources)
          opcode = Opcode::iCALU_FPMULT;
          src1   = (RegType)(rs1);
          src2   = (RegType)(rs2);
          // src3   = (RegType)(insn_raw>>27);
          dst1 = (RegType)(rd);

          break;
        case 0x53:  // XXX - this should prob be its own function FP decode
          opcode = Opcode::iCALU_FPALU;
          src1   = (RegType)(rs1);
          dst1   = (RegType)(rd);

          if (funct7 & 0x20) {
            src2 = RegType::LREG_R0;
          } else {
            src2 = (RegType)(rs2 + 32);
          }

          if (funct7 != 0x60 && funct7 != 0x61 && funct7 != 0x70 && funct7 != 0x71) {
            dst1 = (RegType)(rd + 32);
          } else if (funct7 != 0x68 && funct7 != 0x78 && funct7 != 0x69 && funct7 != 0x79) {
            src1 = (RegType)(rs1 + 32);
          }

          if (funct7 == 8 || funct7 == 9) {
            opcode = Opcode::iCALU_FPMULT;
          } else if (funct7 == 0xC || funct7 == 0xD) {
            opcode = Opcode::iCALU_FPDIV;
          } else if (funct7 == 0x2C || funct7 == 0x2D) {
            opcode = Opcode::iCALU_FPDIV;
            src2   = RegType::LREG_NoDependence;
          }
          break;
        case 0x63:
          opcode = Opcode::iBALU_LBRANCH;
          src1   = (RegType)(rs1);
          src2   = (RegType)(rs2);
          dst1   = RegType::LREG_InvalidOutput;
          break;
        case 0x67:  // jalr
          if (funct3 == 0) {
            opcode = Opcode::iBALU_RJUMP;
            src1   = (RegType)(rs1);
            src2   = RegType::LREG_NoDependence;
            dst1   = (RegType)(rd);

            if (dst1 == RegType::LREG_R0 && src1 == RegType::LREG_R1 && (insn_raw >> 20) == 0) {
              opcode = Opcode::iBALU_RET;
            } else if (dst1 == RegType::LREG_R1) {
              opcode = Opcode::iBALU_RCALL;
            }
          }
          break;
        case 0x6F:
          opcode = Opcode::iBALU_LJUMP;
          src1   = (RegType)(rs1);
          src2   = RegType::LREG_NoDependence;
          dst1   = (RegType)(rd);

          if (dst1 == RegType::LREG_R1) {
            opcode = Opcode::iBALU_LCALL;
          }
          break;
        case 0x73:
          opcode = Opcode::iRALU;
          if (funct3 && funct3 != 0x4) {
            src1 = (RegType)(rs1);
            src2 = RegType::LREG_NoDependence;
            dst1 = (RegType)(rd);
            if (funct3 > 4) {
              src1 = RegType::LREG_NoDependence;
            }
          } else {
            dst1 = RegType::LREG_InvalidOutput;
            src1 = RegType::LREG_NoDependence;
            src2 = RegType::LREG_NoDependence;
          }
          break;
        default: break;
      }
  }

  I(src1 != RegType::LREG_INVALID);
  I(src2 != RegType::LREG_INVALID);
  I(dst1 != RegType::LREG_INVALID);

  uint64_t paddr = 0u;
  uint64_t pc = last[fid].pc;
  if (opcode == Opcode::iLALU_LD || opcode == Opcode::iSALU_ST) {
    paddr = last[fid].addr;
  } else if (opcode == Opcode::iBALU_LBRANCH || opcode == Opcode::iBALU_RBRANCH) {
    paddr = last[fid].next_pc;
    if ((paddr == pc+2) || paddr == pc+4) {
      paddr = 0; // Not taken Control flow instruction
    }
  } else if (opcode == Opcode::iBALU_LJUMP || opcode == Opcode::iBALU_RJUMP || opcode == Opcode::iBALU_LCALL || opcode == Opcode::iBALU_RCALL
             || opcode == Opcode::iBALU_RET) {
    paddr = last[fid].next_pc;
  }

  if (detail > 0) {
    --detail;
    return Dinst::create(Instruction(opcode, src1, src2, dst1, dst2), pc, paddr, fid, false);
  }
  if (time > 0) {
    --time;
    return Dinst::create(Instruction(opcode, src1, src2, dst1, dst2), pc, paddr, fid, true);
  }

  return nullptr;
}
void Emul_dromajo::skip_rabbit(Hartid_t fid, size_t ninst) {
  I(ninst > 0);

  if (ninst > 1) {
    virt_machine_run(machine, fid, ninst - 1);
  }

  execute(fid);
}

void Emul_dromajo::execute(Hartid_t fid) {
  last[fid].pc = machine->cpu_state[fid]->pc;
  (void)riscv_read_insn(machine->cpu_state[fid], &last[fid].insns, last[fid].pc);

  virt_machine_run(machine, fid, 1);

  last[fid].addr    = machine->cpu_state[fid]->last_data_paddr;
  last[fid].next_pc = machine->cpu_state[fid]->pc;
}

Hartid_t Emul_dromajo::get_num() const { return num; }

bool Emul_dromajo::is_sleeping(Hartid_t fid) const {
  fmt::print("called is_sleeping on hartid {}\n", static_cast<int>(fid));
  return true;
}

void Emul_dromajo::init_dromajo_machine() {
  assert(type == "dromajo");

  std::vector<char *> dromajo_args;
  dromajo_args.push_back(strdup("desesc_drom"));
  if (!bench.empty()) {
    dromajo_args.push_back(strdup(bench.c_str()));
  }

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

  for (auto &&item : list_args) {
    if (Config::has_entry(section, item)) {
      std::string arg = "--" + item + "=" + Config::get_string(section, item);
      dromajo_args.push_back(strdup(arg.c_str()));
    }
  }
  char *argv[dromajo_args.size()];
  for (auto i = 0u; i < dromajo_args.size(); ++i) {
    argv[i] = dromajo_args[i];
  }

  fmt::print("\ndromajo arguments:");
  for (const auto *str : dromajo_args) {
    fmt::print(" {}", str);
  }
  fmt::print("\n");

  machine = virt_machine_main(dromajo_args.size(), argv);

  for (auto *ptr : dromajo_args) {
    free(ptr);
  }
}
