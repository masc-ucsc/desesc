// See LICENSE for details.

#pragma once

#include <stdint.h>

#include <algorithm>
#include <vector>

#include "iassert.hpp"
#include "opcode.hpp"

class Instruction {
private:
protected:
  Opcode  opcode;
  RegType src1;
  RegType src2;
  RegType dst1;
  RegType dst2;

public:
  static const char *opcode2Name(Opcode type);
  void               set(Opcode op, RegType src1, RegType src2, RegType dst1, RegType dst2);
  bool               doesJump2Label() const {
                  return opcode == iBALU_LJUMP || opcode == iBALU_LCALL;
  }  // No branch label, just unconditional instructions
  bool        doesCtrl2Label() const { return opcode == iBALU_LJUMP || opcode == iBALU_LCALL || opcode == iBALU_LBRANCH; }
  Opcode      getOpcode() const { return opcode; }
  const char *getOpcodeName() const { return opcode2Name(opcode); }
  void        forcemult() { opcode = iCALU_FPMULT; }

  RegType getSrc1() const { return src1; }
  RegType getSrc2() const { return src2; }
  RegType getDst1() const { return dst1; }
  RegType getDst2() const { return dst2; }

  // if dst == Invalid -> dst2 == invalid
  bool hasDstRegister() const { return dst1 != LREG_InvalidOutput || dst2 != LREG_InvalidOutput; }
  bool hasSrc1Register() const { return src1 != LREG_NoDependence; }
  bool hasSrc2Register() const { return src2 != LREG_NoDependence; }
  int  getnsrc() const {
     int n = hasSrc1Register() ? 1 : 0;
     n += hasSrc2Register() ? 1 : 0;
     return n;
  }

  bool isFuncCall() const { return opcode == iBALU_RCALL || opcode == iBALU_LCALL; }
  bool isFuncRet() const { return opcode == iBALU_RET; }
  // All the conditional control flow instructions are branches
  bool isBranch() const { return opcode == iBALU_RBRANCH || opcode == iBALU_LBRANCH; }
  // All the unconditional but function return are jumps
  bool isJump() const { return opcode == iBALU_RJUMP || opcode == iBALU_LJUMP || isFuncCall(); }

  bool isControl() const {
    GI(opcode >= iBALU_LBRANCH && opcode <= iBALU_RET, isJump() || isBranch() || isFuncCall() || isFuncRet());

    return opcode >= iBALU_LBRANCH && opcode <= iBALU_RET;
  }

  bool isALU() const { return opcode == iAALU; }
  bool isLoad() const { return opcode == iLALU_LD; }
  bool isStore() const { return opcode == iSALU_ST || opcode == iSALU_SC || opcode == iSALU_LL; }
  bool isComplex() const { return opcode >= iCALU_FPMULT && opcode <= iCALU_DIV; }
  bool isStoreAddress() const { return opcode == iSALU_ADDR; }

  bool isMemory() const { return isLoad() || isStore(); }

  void dump(const char *str) const;
};
