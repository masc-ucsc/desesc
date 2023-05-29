// See LICENSE for details.

#pragma once

#include <stdint.h>

#include <algorithm>
#include <string>
#include <vector>

#include "iassert.hpp"
#include "opcode.hpp"

class Instruction {
private:
  Opcode  opcode;
  RegType src1;
  RegType src2;
  RegType dst1;
  RegType dst2;

public:
  constexpr Instruction(Opcode op, RegType _src1, RegType _src2, RegType _dst1, RegType _dst2)
      : opcode(op), src1(_src1), src2(_src2), dst1(_dst1), dst2(_dst2) {}

  static std::string opcode2Name(Opcode type);
  void               set(Opcode op, RegType src1, RegType src2, RegType dst1, RegType dst2);
  [[nodiscard]] bool doesJump2Label() const {
    return opcode == Opcode::iBALU_LJUMP || opcode == Opcode::iBALU_LCALL;
  }  // No branch label, just unconditional instructions
  [[nodiscard]] bool doesCtrl2Label() const {
    return opcode == Opcode::iBALU_LJUMP || opcode == Opcode::iBALU_LCALL || opcode == Opcode::iBALU_LBRANCH;
  }
  [[nodiscard]] Opcode      getOpcode() const { return opcode; }
  [[nodiscard]] std::string getOpcodeName() const { return opcode2Name(opcode); }
  void                      forcemult() { opcode = Opcode::iCALU_FPMULT; }

  [[nodiscard]] RegType getSrc1() const { return src1; }
  [[nodiscard]] RegType getSrc2() const { return src2; }
  [[nodiscard]] RegType getDst1() const { return dst1; }
  [[nodiscard]] RegType getDst2() const { return dst2; }

  // if dst == Invalid -> dst2 == invalid
  [[nodiscard]] bool hasDstRegister() const { return dst1 != RegType::LREG_InvalidOutput || dst2 != RegType::LREG_InvalidOutput; }
  [[nodiscard]] bool hasSrc1Register() const { return src1 != RegType::LREG_NoDependence; }
  [[nodiscard]] bool hasSrc2Register() const { return src2 != RegType::LREG_NoDependence; }
  [[nodiscard]] int  getnsrc() const {
    int n = hasSrc1Register() ? 1 : 0;
    n += hasSrc2Register() ? 1 : 0;
    return n;
  }

  [[nodiscard]] bool isFuncCall() const { return opcode == Opcode::iBALU_RCALL || opcode == Opcode::iBALU_LCALL; }
  [[nodiscard]] bool isFuncRet() const { return opcode == Opcode::iBALU_RET; }
  // All the conditional control flow instructions are branches
  [[nodiscard]] bool isBranch() const { return opcode == Opcode::iBALU_RBRANCH || opcode == Opcode::iBALU_LBRANCH; }
  // All the unconditional but function return are jumps
  [[nodiscard]] bool isJump() const { return opcode == Opcode::iBALU_RJUMP || opcode == Opcode::iBALU_LJUMP || isFuncCall(); }

  [[nodiscard]] bool isControl() const {
    GI(opcode >= Opcode::iBALU_LBRANCH && opcode <= Opcode::iBALU_RET, isJump() || isBranch() || isFuncCall() || isFuncRet());

    return opcode >= Opcode::iBALU_LBRANCH && opcode <= Opcode::iBALU_RET;
  }

  [[nodiscard]] bool isALU() const { return opcode == Opcode::iAALU; }
  [[nodiscard]] bool isLoad() const { return opcode == Opcode::iLALU_LD; }
  [[nodiscard]] bool isStore() const {
    return opcode == Opcode::iSALU_ST || opcode == Opcode::iSALU_SC || opcode == Opcode::iSALU_LL;
  }
  [[nodiscard]] bool isComplex() const { return opcode >= Opcode::iCALU_FPMULT && opcode <= Opcode::iCALU_DIV; }
  [[nodiscard]] bool isStoreAddress() const { return opcode == Opcode::iSALU_ADDR; }

  [[nodiscard]] bool isMemory() const { return isLoad() || isStore(); }

  void                      dump() const;
  [[nodiscard]] std::string get_asm() const;
};
