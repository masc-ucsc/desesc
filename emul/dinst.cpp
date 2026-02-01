// See LICENSE for details.

#include "dinst.hpp"

#include <math.h>

#include "emul_base.hpp"
#include "fmt/format.h"
#include "iassert.hpp"
#include "tracer.hpp"

pool<Dinst> Dinst::dInstPool(32768, "Dinst");  // 4 * tsfifo size

Dinst::Dinst()
    : inst(Instruction(Opcode::iOpInvalid, RegType::LREG_R0, RegType::LREG_R0, RegType::LREG_InvalidOutput,
                       RegType::LREG_InvalidOutput)) {
  pend[0].init(this);
  pend[1].init(this);
  pend[2].init(this);
  I(MAX_PENDING_SOURCES == 3);
}

void Dinst::dump(std::string_view str) {
  fmt::print("{} ({}) {} {} Dinst: pc=0x{:x}, addr=0x{:x} src1={} ({}) src2={} dest1={} dest2={}",
             str,
             fid,
             (long long)ID,
             keep_stats ? 't' : 'd',
             (long long)pc,
             (long long)addr,
             (int)(inst.getSrc1()),
             inst.getOpcodeName(),
             inst.getSrc2(),
             inst.getDst1(),
             inst.getDst2());

  Time_t t;

  t = getRenamedTime() - getFetchTime();
  if (getRenamedTime()) {
    fmt::print(" %5d", (int)t);
  } else {
    fmt::print("    na");
  }

  t = getIssuedTime() - getRenamedTime();
  if (getIssuedTime()) {
    fmt::print(" %5d", (int)t);
  } else {
    fmt::print("    na");
  }

  t = getExecutedTime() - getIssuedTime();
  if (getExecutedTime()) {
    fmt::print(" %5d", (int)t);
  } else {
    fmt::print("    na");
  }

  t = globalClock - getExecutedTime();
  if (getExecutedTime()) {
    fmt::print(" %5d", (int)t);
  } else {
    fmt::print("    na");
  }

  if (performed) {
    fmt::print(" performed");
  } else if (executing) {
    fmt::print(" executing");
  } else if (executed) {
    fmt::print(" executed");
  } else if (issued) {
    fmt::print(" issued");
  } else {
    fmt::print(" non-issued");
  }
  if (replay) {
    fmt::print(" REPLAY ");
  }

  if (hasPending()) {
    fmt::print(" has pending");
  }
  if (!isSrc1Ready()) {
    fmt::print(" has src1 deps");
  }
  if (!isSrc2Ready()) {
    fmt::print(" has src2 deps");
  }
  if (!isSrc3Ready()) {
    fmt::print(" has src3 deps");
  }

  // inst.dump("Inst->");

  fmt::print("\n");
}

void Dinst::clearRATEntry() {
  I(RAT1Entry);
  if ((*RAT1Entry) == this) {
    *RAT1Entry = nullptr;
  }
  if ((*RAT2Entry) == this) {
    *RAT2Entry = nullptr;
  }
  if (serializeEntry) {
    if ((*serializeEntry) == this) {
      *serializeEntry = nullptr;
    }
  }
}

#ifdef ESESC_TRACE_DATA
DataSign Dinst::calcDataSign(int64_t _data) {
  DataSign data_sign;
  int64_t  hash, code;

  if (_data == 0) {
    data_sign = DS_V0;
  } else if (_data == 1) {
    data_sign = DS_P1;
  } else if (_data == 2) {
    data_sign = DS_P2;
  } else if (_data == 3) {
    data_sign = DS_P3;
  } else if (_data == 4) {
    data_sign = DS_P4;
  } else if (_data == 5) {
    data_sign = DS_P5;
  } else if (_data == 6) {
    data_sign = DS_P6;
  } else if (_data == 7) {
    data_sign = DS_P7;
  } else if (_data == 8) {
    data_sign = DS_P8;
  } else if (_data == 9) {
    data_sign = DS_P9;
  } else if (_data == 10) {
    data_sign = DS_P10;
  } else if (_data == 11) {
    data_sign = DS_P11;
  } else if (_data == 12) {
    data_sign = DS_P12;
  } else if (_data == 13) {
    data_sign = DS_P13;
  } else if (_data == 14) {
    data_sign = DS_P14;
  } else if (_data == 15) {
    data_sign = DS_P15;
  } else if (_data == 16) {
    data_sign = DS_P16;
  } else if (_data == 32) {
    data_sign = DS_P32;
  } else if (_data == -1) {
    data_sign = DS_N1;
  } else if (_data == -2) {
    data_sign = DS_N2;
  } else if (_data % 5 == 0) {
    data_sign = DS_FIVE;
  } else if (std::ceil(log2(_data)) == std::floor(log2(_data))) {  // if data is power of 2
    data_sign = DS_POW;
  } else if (_data > 1024 * 1024 || _data < -1024 * 1024) {
    data_sign = DS_PTR;
  } else {
    int v     = static_cast<int>(DS_OPos) + (_data % 255);
    data_sign = static_cast<DataSign>(v);
  }
  return data_sign;
}

void Dinst::setDataSign(int64_t _data, Addr_t _ldpc) {
  ldpc      = _ldpc;
  data_sign = calcDataSign(_data);
}

void Dinst::addDataSign(int ds, int64_t _data, Addr_t _ldpc) {
  ldpc = (ldpc << 4) ^ _ldpc;

  if (ds == 0) {
    if (_data == data) {
      data_sign = DS_EQ;
    } else if (data < _data) {
      data_sign = DS_LTC;
    } else if (data > _data) {
      data_sign = DS_GEC;
    } else {
      I(_data != data);
      data_sign = DS_NE;
    }
  } else if (ds == 1) {
    Data_t mix = data ^ (_data << 3);
    data       = mix;
    int v      = static_cast<int>(DS_OPos) + (data % 255);
    data_sign  = static_cast<DataSign>(v);
  }
}
#endif

void Dinst::scrap() {
  I(nDeps == 0);
  I(first == nullptr);

  resource = nullptr;
  cluster  = nullptr;

  dInstPool.in(this);
}

void Dinst::destroy() {
  I(nDeps == 0);

  I(issued);
  I(executed);

  I(first == nullptr);

  Tracer::commit(this);

  resource = nullptr;
  cluster  = nullptr;
  dInstPool.in(this);
}
void Dinst::destroyTransientInst() {
  I(first == nullptr);

  Tracer::commit(this);
  resource = nullptr;
  cluster  = nullptr;

  dInstPool.in(this);
}
