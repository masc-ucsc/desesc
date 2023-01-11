// See LICENSE for details.

#include "dinst.hpp"

#include <math.h>

#include "emul_base.hpp"
#include "fmt/format.h"
#include "iassert.hpp"

pool<Dinst> Dinst::dInstPool(32768, "Dinst");  // 4 * tsfifo size

Time_t Dinst::currentID = 0;

Dinst::Dinst() {
  pend[0].init(this);
  pend[1].init(this);
  pend[2].init(this);
  I(MAX_PENDING_SOURCES == 3);
  nDeps = 0;
}

void Dinst::dump(const char *str) {
  fprintf(stderr,
          "%s:%p (%d) %lld %c Dinst: pc=0x%llx, addr=0x%llx src1=%d (%s) src2 = %d dest1 =%d dest2 = %d",
          str,
          this,
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
    fprintf(stderr, " %5d", (int)t);
  } else {
    fprintf(stderr, "    na");
  }

  t = getIssuedTime() - getRenamedTime();
  if (getIssuedTime()) {
    fprintf(stderr, " %5d", (int)t);
  } else {
    fprintf(stderr, "    na");
  }

  t = getExecutedTime() - getIssuedTime();
  if (getExecutedTime()) {
    fprintf(stderr, " %5d", (int)t);
  } else {
    fprintf(stderr, "    na");
  }

  t = globalClock - getExecutedTime();
  if (getExecutedTime()) {
    fprintf(stderr, " %5d", (int)t);
  } else {
    fprintf(stderr, "    na");
  }

  if (performed) {
    fprintf(stderr, " performed");
  } else if (executing) {
    fprintf(stderr, " executing");
  } else if (executed) {
    fprintf(stderr, " executed");
  } else if (issued) {
    fprintf(stderr, " issued");
  } else {
    fprintf(stderr, " non-issued");
  }
  if (replay) {
    fprintf(stderr, " REPLAY ");
  }

  if (hasPending()) {
    fprintf(stderr, " has pending");
  }
  if (!isSrc1Ready()) {
    fprintf(stderr, " has src1 deps");
  }
  if (!isSrc2Ready()) {
    fprintf(stderr, " has src2 deps");
  }
  if (!isSrc3Ready()) {
    fprintf(stderr, " has src3 deps");
  }

  // inst.dump("Inst->");

  fprintf(stderr, "\n");
}

void Dinst::clearRATEntry() {
  I(RAT1Entry);
  if ((*RAT1Entry) == this) {
    *RAT1Entry = 0;
  }
  if ((*RAT2Entry) == this) {
    *RAT2Entry = 0;
  }
  if (serializeEntry) {
    if ((*serializeEntry) == this) {
      *serializeEntry = 0;
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
  /// data = _data;
  ldpc = _ldpc;

  data_sign = calcDataSign(_data);
  // br_ld_chain_predictable = true; //FIXME - LDBP does prediction only if this flag is set(when load is predictable)
}

void Dinst::addDataSign(int ds, int64_t _data, Addr_t _ldpc) {
  ldpc = (ldpc << 4) ^ _ldpc;

  if (ds == 0) {
    /*if (_data == data)
      data_sign = DS_EQ;
    else if (_data >= data)
      data_sign = DS_GEC;
    else if (_data < data)
      data_sign = DS_LTC;
    else if (_data != data)
      data_sign = DS_NE;*/  // FIXME: add DS_LT, DS_LE, DS...

    if (_data == data) {  // beq; rs(data) == rt(_data)
      data_sign = DS_EQ;
    } else if (data < _data) {  // bltc; rs < rt (bgtc alias for bltc)
      data_sign = DS_LTC;
    } else if (data > _data) {  // bgec; rs >= rt (blec is alias for bgec)
      data_sign = DS_GEC;
    } else {
      I(_data != data);  // bne; rs ! = rt
      data_sign = DS_NE;
    }
  } else if (ds == 1) {
    // Data_t mix = data ^ (_data<<3) + (data>>1);
    Data_t mix = data ^ (_data << 3);
    data       = mix;
    int v      = static_cast<int>(DS_OPos) + (data % 255);
    data_sign  = static_cast<DataSign>(v);
  } else {
    // Do not mix
  }
}
#endif

Dinst *Dinst::clone() {
  Dinst *i = dInstPool.out();

  i->fid  = fid;
  i->inst = inst;
  i->pc   = pc;
  i->addr = addr;
#ifdef ESESC_TRACE_DATA
  i->ldpc      = ldpc;
  i->data      = data;
  i->data_sign = data_sign;
  i->chained   = 0;
#endif
  i->keep_stats = keep_stats;

  i->setup();

  return i;
}

void Dinst::recycle() {
  I(nDeps == 0);  // No deps src
  I(first == 0);  // no dependent instructions


  dInstPool.in(this);
}

void Dinst::scrap() {
  I(nDeps == 0);  // No deps src
  I(first == 0);  // no dependent instructions

  resource = nullptr; // Needed to have GC
  cluster = nullptr;

  dInstPool.in(this);
}

void Dinst::destroy() {
  I(nDeps == 0);  // No deps src

  I(issued);
  I(executed);

  I(first == 0);  // no dependent instructions

  resource = nullptr; // Needed to have GC
  cluster = nullptr;

  dInstPool.in(this);
}
