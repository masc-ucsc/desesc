// See LICENSE for details.

#include "emul_dromajo.hpp"

#include <assert.h>
#include <stdio.h>

Emul_dromajo::Emul_dromajo(Config conf) : Emul_base(conf) {
  assert(type == "dromajo");
}

Emul_dromajo::~Emul_dromajo()
/* Emul_dromajo destructor */
{}

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

Dinst *Emul_dromajo::peek(Hartid_t fid) {
  uint64_t last_pc  = virt_machine_get_pc(machine, fid);
  uint32_t insn_raw = -1;
  (void)riscv_read_insn(machine->cpu_state[fid], &insn_raw, last_pc);
  Instruction esesc_insn;

  uint32_t funct7  = (insn_raw >> 25) & 0x7F;
  uint32_t funct3  = (insn_raw >> 12) & 0x7;
  uint32_t rs1     = (insn_raw >> 15) & 0x1F;
  uint32_t rs2     = (insn_raw >> 20) & 0x1F;
  uint32_t rd      = (insn_raw >> 7) & 0x1F;
  Opcode   opcode  = iAALU;  // dromajo wont pass invalid insns, default to ALU
  RegType  src1    = (RegType)(rs1);
  RegType  src2    = (RegType)(rs2);
  RegType  dst1    = (RegType)(rd);
  RegType  dst2    = LREG_InvalidOutput;
  Addr_t   address = 0;
  switch (insn_raw & 0x3) {  // compressed
    case 0x0:                // C0
      break;
    case 0x1: break;        // XXX - this should prob be its own function
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
            src2    = LREG_InvalidOutput;
            address = I_type_addr_decode(insn_raw) + virt_machine_get_reg(machine, fid, rs1);
          }
          break;
        case 0x07: //   FP Load
          if (funct3 == 3 || funct3 == 4) {
            opcode  = iLALU_LD;
            //dst1   += LREG_FP0;
            address = I_type_addr_decode(insn_raw) + virt_machine_get_reg(machine, fid, rs1);
          }
          break;
        case 0x0F: opcode = iRALU; break;
        case 0x13:
          src2   = LREG_InvalidOutput;
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
            src2 = LREG_InvalidOutput;
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
        case 0x53: // XXX - this should prob be its own function
          opcode = iCALU_FPALU;
          //src1  += LREG_FP0;
          //src2  += LREG_FP0;
          //dst1   += LREG_FP0;
          if (funct7 == 8 || funct7 == 9) {
              opcode = iCALU_FPMULT;
          } else if (funct7 == 0xC) {
              opcode = iCALU_FPDIV;
          } else if (funct7 == 0x2C) {
              opcode = iCALU_FPDIV;
              src2   = LREG_InvalidOutput;
          } else if (funct7 & 0x20) {
              src2   = LREG_InvalidOutput;
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
            src2    = LREG_InvalidOutput;
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
          src1 = src2 = LREG_InvalidOutput;
          address = UJ_type_addr_decode(funct7, rs2, rs1, funct3) + last_pc;

          if (dst1 == LREG_R1) {
            opcode = iBALU_LCALL;
          }
          break;
        case 0x73:
          opcode = iRALU;
          if (funct3 && funct3 != 0x4) {
              src2 = LREG_InvalidOutput;
            if (funct3 > 4) {
              src1 = LREG_InvalidOutput;
            }
          } else {
              dst1 = src1 = src2 = LREG_InvalidOutput;
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
