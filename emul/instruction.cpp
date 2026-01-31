// See LICENSE for details.

#include "instruction.hpp"

#include "fmt/format.h"
#include "iassert.hpp"

std::string Instruction::opcode2Name(Opcode op) { return fmt::format("{}", op); }

void Instruction::dump() const { fmt::print("{}", get_asm()); }

std::string Instruction::get_asm() const {
  if (dst2 != RegType::LREG_InvalidOutput) {
    if (src2 == RegType::LREG_R0) {
      return fmt::format("r{}, r{} = {} r{}", dst1, dst2, opcode, src1);
    }

    return fmt::format("r{}, r{} = {} r{} r{}", dst1, dst2, opcode, src1, src2);
  }

  if (src2 == RegType::LREG_R0) {
    return fmt::format("r{} = {} r{}", dst1, opcode, src1);
  }

  return fmt::format("r{} = {} r{} r{}", dst1, opcode, src1, src2);
}

void Instruction::set(Opcode opcode_, RegType src1_, RegType src2_, RegType dst1_, RegType dst2_) {
  I(opcode_ != Opcode::iOpInvalid);

  opcode = opcode_;
  src1   = src1_;
  src2   = src2_;
  if (dst1_ == LREG_NoDependence) {
    dst1 = RegType::LREG_InvalidOutput;
  } else {
    dst1 = dst1_;
  }
  if (dst2_ == LREG_NoDependence) {
    dst2 = RegType::LREG_InvalidOutput;
  } else {
    dst2 = dst2_;
  }
}
