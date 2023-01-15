// See LICENSE for details.

#include <filesystem>
#include "emul_dromajo.hpp"
#include "absl/strings/str_split.h"

Emul_dromajo::Emul_dromajo() : Emul_base() {
  num = 0;

  uint64_t rabbit = 0;

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
  if (bench_split.empty() || !std::filesystem::exists(bench_split[0])) {
    Config::add_error(fmt::format("could not open dromajo bench={}\n", bench));
    if (!std::filesystem::exists(bench_split[0])) {
      Config::add_error(fmt::format("file {} not accessible from {} path\n", bench, std::filesystem::current_path().u8string()));
    }
  }
  Config::exit_on_error();
  type = "dromajo";
  if (num) {
    init_dromajo_machine();
  }
  if (rabbit) {
    for(auto i=0u;i<num;++i) {
      skip_rabbit(i, rabbit);
    }
  }
}

Emul_dromajo::~Emul_dromajo() {}

void Emul_dromajo::destroy_machine() {
  if (machine != NULL) {
    virt_machine_end(machine);
  }
  // XXX - dromajo has a memory leak, needs to be fixed on that end
}

static inline Addr_t I_type_addr_decode(uint32_t insn_raw) {
  Addr_t address = insn_raw >> 20;
  address |= (~address & 0x800) + 0xFFFFFFFFFFFFF800ull;
  return address;
}

static inline Addr_t S_type_addr_decode(uint32_t funct7, uint32_t rd) {
  Addr_t address = (funct7 << 5) | rd;
  address |= (~address & 0x800) + 0xFFFFFFFFFFFFF800ull;
  return address;
}

static inline Addr_t SB_type_addr_decode(uint32_t funct7, uint32_t rd) {
  Addr_t address = ((funct7 & 0x40) << 6) | ((rd & 1) << 11)
                 | ((funct7 & 0x3F) << 5) | (rd & 0x1E);
  address |= (~address & 0x1000) + 0xFFFFFFFFFFFFF000ull;
  return address;
}

static inline Addr_t UJ_type_addr_decode(uint32_t funct7, uint32_t rs2, uint32_t rs1, uint32_t funct3) {
  Addr_t address = ((funct7 & 0x40) << 14) | (rs1 << 15)
                          | (funct3 << 12) | ((rs2 & 1) << 11)
                  | ((funct7 & 0x3F) << 5) | (rs2 & 0x1E);
  address |= (~address & 0x100000) + 0xFFFFFFFFFFF00000ull;
  return address;
}

static inline uint32_t C_reg_decode(uint32_t rn) {
  return rn + 8;
}

static inline Addr_t C0_addr_decode(uint16_t insn_raw, uint16_t opcode) {
  Addr_t address = (insn_raw & 0x1C00) >> 7;
  if (opcode == 2 || opcode == 6) {
    address |= (insn_raw & 0x40) >> 4;
    address |= (insn_raw & 0x20) << 1;
    address |= (~address & 0x40) + 0xFFFFFFFFFFFFFFC0ull;
  } else {
    address |= (insn_raw & 0x60) << 1;
    address |= (~address & 0x80) + 0xFFFFFFFFFFFFFF80ull;
  }
  return address;
}

static inline Addr_t C1_j_addr_decode(uint16_t insn_raw) {
  Addr_t address = ((insn_raw & 0x1000) >> 1) | ((insn_raw & 0x100) << 2)
                  | ((insn_raw & 0x600) >> 1) | ((insn_raw & 0x40) << 1)
                  | ((insn_raw & 0x80) >> 1)  | ((insn_raw & 0x4) << 3)
                  | ((insn_raw & 0x800) >> 7) | ((insn_raw & 0x38) >> 2);
  address |= (~address & 0x800) + 0xFFFFFFFFFFFFF800ull;
  return address;
}

static inline Addr_t C1_br_addr_decode(uint16_t insn_raw) {
  Addr_t address = ((insn_raw & 0x1000) >> 4) | ((insn_raw & 0x60) << 1)
                    | ((insn_raw & 0x4) << 3) | ((insn_raw & 0xC00) >> 7)
                    | ((insn_raw & 0x18) >> 2);
  address |= (~address & 0x100) + 0xFFFFFFFFFFFFFF00ull;
  return address;
}

static inline Addr_t C2_ldsp_addr_decode(uint16_t insn_raw) {
  Addr_t address = ((insn_raw & 0x1C) << 4) | ((insn_raw & 0x1000) >> 7)
                                            | ((insn_raw & 0x60) >> 2);
  address |= (~address & 0x100) + 0xFFFFFFFFFFFFFF00ull;
  return address;
}

static inline Addr_t C2_lwsp_addr_decode(uint16_t insn_raw) {
  Addr_t address = ((insn_raw & 0xC) << 4) | ((insn_raw & 0x1000) >> 7)
                                           | ((insn_raw & 0x70) >> 2);
  address |= (~address & 0x80) + 0xFFFFFFFFFFFFFF80ull;
  return address;
}

static inline Addr_t C2_sdsp_addr_decode(uint16_t insn_raw) {
  Addr_t address = ((insn_raw & 0x380) >> 1) | ((insn_raw & 0x1C00) >> 7);
  address |= (~address & 0x100) + 0xFFFFFFFFFFFFFF00ull;
  return address;
}

static inline Addr_t C2_swsp_addr_decode(uint16_t insn_raw) {
  Addr_t address = ((insn_raw & 0x180) >> 1) | ((insn_raw & 0x1E00) >> 7);
  address |= (~address & 0x80) + 0xFFFFFFFFFFFFFF80ull;
  return address;
}

Dinst *Emul_dromajo::peek(Hartid_t fid) {

  uint64_t last_pc  = virt_machine_get_pc(machine, fid);
  uint32_t insn_raw = -1;
  (void)riscv_read_insn(machine->cpu_state[fid], &insn_raw, last_pc);
  Instruction esesc_insn;

  uint32_t funct3, rs1, rs2, rd;
  uint32_t funct7 = (insn_raw >> 13) & 0x7;
  Opcode  opcode  = iAALU;  // dromajo wont pass invalid insns, default to ALU
  RegType src1, src2, dst1;
  RegType dst2    = LREG_InvalidOutput;
  Addr_t  address = 0;
  switch (insn_raw & 0x3) {  // compressed
    case 0x0:                // C0
      rs1     = C_reg_decode((insn_raw >> 7) & 0x7);
      rd      = C_reg_decode((insn_raw >> 2) & 0x7);
      src1    = (RegType)(rs1);
      address = C0_addr_decode(insn_raw, funct7) + virt_machine_get_reg(machine, fid, rs1);

      if (funct7 == 1 || funct7 == 5) { // FP LD/ST
        rd += 32;
      }

      if (funct7 >= 5) {
        opcode = iSALU_ST;
        src2   = (RegType)(rd);
        dst1   = LREG_InvalidOutput;
      } else {
        opcode = iLALU_LD;
        src2   = LREG_NoDependence;
        dst1   = (RegType)(rd);
      }
      break;
    case 0x1:              // C1
      src2   = LREG_NoDependence;

      if (insn_raw == 1) { // NOP
        src1 = LREG_NoDependence;
        dst1 = LREG_InvalidOutput;
      } else if (funct7 == 5) {
        opcode  = iBALU_LJUMP;
        src1    = LREG_NoDependence;
        dst1    = LREG_InvalidOutput;
        address = C1_j_addr_decode(insn_raw) + last_pc;
      } else if (funct7 < 4) {
        rs1  = (insn_raw >> 7) & 0x1F;
        src1 = dst1 = (RegType)(rs1);
      } else {
        rs1    = C_reg_decode((insn_raw >> 7) & 0x7);
        src1   = (RegType)(rs1);
        funct3 = (insn_raw >> 10) & 0x3;

        if (funct7 == 4) {
          rd = src1;

          if (funct3 == 3) {
            rs2  = C_reg_decode((insn_raw >> 2) & 0x7);
            src2 = (RegType)(rs2);
          }
        } else {
          address = C1_br_addr_decode(insn_raw) + last_pc;
          opcode  = iBALU_LBRANCH;
        }
      }
      break;
    case 0x2:
      rd  = (insn_raw >> 7) & 0x1F;
      rs2 = (insn_raw >> 2) & 0x1F;

      if (funct7 == 1 || funct7 == 5) {
        rd  += 32;    // FP LD/ST
        rs2 += 32;
      }

      if (insn_raw == 0x9002) {  // ebreak
        dst1 = LREG_InvalidOutput;
        src1 = src2 = LREG_NoDependence;
      } else if (funct7 < 4) {
        dst1 = (RegType)(rd);
        src2 = LREG_NoDependence;

        if (funct7 != 0) {
          src1    = (RegType)(2);
          opcode  = iLALU_LD;
          address = virt_machine_get_reg(machine, fid, 2);

          if (funct7 == 2) {
            address += C2_lwsp_addr_decode(insn_raw);
          } else {
            address += C2_ldsp_addr_decode(insn_raw);
          }
        } else {
          src1 = (RegType)(rd);
        }
      } else if (funct7 > 4) {
        src2    = (RegType)(rs2);
        dst1    = LREG_InvalidOutput;
        src1    = (RegType)(2);
        opcode  = iSALU_ST;
        address = virt_machine_get_reg(machine, fid, 2);

        if (funct7 == 6) {
          address += C2_swsp_addr_decode(insn_raw);
        } else {
          address += C2_sdsp_addr_decode(insn_raw);
        }
      } else {
        funct3 = (insn_raw >> 12) & 0x1;
        rs1    = rd;
        src1   = (RegType)(rs1);

        if (funct3 == 0 && rs2 == 0) {   // C.JR
          src2    = LREG_NoDependence;
          dst1    = (RegType)(0);
          opcode  = iBALU_RJUMP;
          address = virt_machine_get_reg(machine, fid, rs1);

          if (src1 == LREG_R1) {
            opcode = iBALU_RET;
          }
        } else if (funct3 == 1 && rs2 == 0) {  // C.JALR
          src2    = LREG_NoDependence;
          dst1    = (RegType)(1);
          opcode  = iBALU_RJUMP;
          address = virt_machine_get_reg(machine, fid, rs1);
        } else {
          if (funct3 == 0) {
            src1 = (RegType)(0);
          }
          src2 = (RegType)(rs2);
          dst1 = (RegType)(rd);
        }
      }
      break;
    default:
      funct7  = (insn_raw >> 25) & 0x7F;
      funct3  = (insn_raw >> 12) & 0x7;
      rs1     = (insn_raw >> 15) & 0x1F;
      rs2     = (insn_raw >> 20) & 0x1F;
      rd      = (insn_raw >> 7) & 0x1F;
      src1    = (RegType)(rs1);
      src2    = (RegType)(rs2);
      dst1    = (RegType)(rd);
      // TO-DO: the rest of floating point insns
      switch (insn_raw & 0x7F) {
        case 0x03:
          if (funct3 < 6) {
            opcode  = iLALU_LD;
            src2    = LREG_NoDependence;
            address = I_type_addr_decode(insn_raw) + virt_machine_get_reg(machine, fid, rs1);
          }
          break;
        case 0x07: //   FP Load
          if (funct3 == 3 || funct3 == 4) {
            opcode  = iLALU_LD;
            src2    = LREG_NoDependence;
            dst1    = (RegType)(rd + 32);
            address = I_type_addr_decode(insn_raw) + virt_machine_get_reg(machine, fid, rs1);
          }
          break;
        case 0x0F: opcode = iRALU; break;
        case 0x13:
          src2   = LREG_NoDependence;
          break;
        case 0x23:
          if (funct3 < 4) {
            opcode  = iSALU_ST;
            dst1    = LREG_InvalidOutput;
            address = S_type_addr_decode(funct7, rd) + virt_machine_get_reg(machine, fid, rs1);
          }
          break;
        case 0x2F:
          opcode = iRALU;
          if (!rs2) {
            src2 = LREG_NoDependence;
          }
          break;
        case 0x33:
          if (funct7 == 1) {
            if (funct3 < 4) {
              opcode = iCALU_MULT;
            } else {
              opcode = iCALU_DIV;
            }
          }
          break;
        case 0x3b:
          if (funct7 == 1) {
            if (funct3 == 0) {
              opcode = iCALU_MULT;
            } else if (funct3 > 3) {
              opcode = iCALU_DIV;
            }
          }
          break;
        case 0x47: // FP Store
          opcode  = iSALU_ST;
          src2    = (RegType)(rs2 + 32);
          dst1    = LREG_InvalidOutput;
          address = S_type_addr_decode(funct7, rd) + virt_machine_get_reg(machine, fid, rs1);
          break;
        case 0x53: // XXX - this should prob be its own function FP decode
          opcode = iCALU_FPALU;

          if (funct7 & 0x20) {
            src2   = LREG_R0;
          } else {
            src2   = (RegType)(rs2 + 32);
          }

          if (funct7 != 0x60 && funct7 != 0x61 && funct7 != 0x70 && funct7 != 0x71) {
            dst1   = (RegType)(rd + 32);
          } else if (funct7 != 0x68 && funct7 != 0x78 && funct7 != 0x69 && funct7 != 0x79) {
            src1   = (RegType)(rs1 + 32);
          }

          if (funct7 == 8 || funct7 == 9) {
              opcode = iCALU_FPMULT;
          } else if (funct7 == 0xC || funct7 == 0xD) {
              opcode = iCALU_FPDIV;
          } else if (funct7 == 0x2C || funct7 == 0x2D) {
              opcode = iCALU_FPDIV;
              src2   = LREG_NoDependence;
          }
          break;
        case 0x63:
          opcode  = iBALU_LBRANCH;
          address = SB_type_addr_decode(funct7, rd) + last_pc;
          dst1    = LREG_InvalidOutput;
          break;
        case 0x67:      // jalr
          if (funct3 == 0) {
            opcode  = iBALU_RJUMP;
            src2    = LREG_NoDependence;
            address = I_type_addr_decode(insn_raw);

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
          src1 = src2 = LREG_NoDependence;
          address = UJ_type_addr_decode(funct7, rs2, rs1, funct3) + last_pc;

          if (dst1 == LREG_R1) {
            opcode = iBALU_LCALL;
          }
          break;
        case 0x73:
          opcode = iRALU;
          if (funct3 && funct3 != 0x4) {
              src2 = LREG_NoDependence;
            if (funct3 > 4) {
              src1 = LREG_NoDependence;
            }
          } else {
              dst1 = LREG_InvalidOutput;
              src1 = LREG_NoDependence;
              src2 = LREG_NoDependence;
          }
          break;
        default: break;
      }
  }

  // assert(dst1 != LREG_R0);

  esesc_insn.set(opcode, src1, src2, dst1, dst2);
  if (detail>0) {
    --detail;
    return Dinst::create(&esesc_insn, last_pc, address, fid, false);
  }
  if (time>0) {
    --time;
    return Dinst::create(&esesc_insn, last_pc, address, fid, true);
  }

  return nullptr;
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
