// See LICENSE for details.

#pragma once

#include "stats.hpp"
#include "dinst.hpp"
#include "estl.h"
#include "iassert.hpp"

//#define DEBUG_STRIDESO2 1
//#define UNLIMITED_BIMODAL 1

#define VTAGE_LOGG    10 /* logsize of the  tagged TAGE tables*/
#define VTAGE_UWIDTH  2
#define VTAGE_CWIDTH  3
#define VTAGE_MAXHIST 14
#define VTAGE_MINHIST 1
#define VTAGE_TBITS   12  // minimum tag width

#define STRIDE_DELTA0 1

class Dinst;
class MemObj;

class AddressPredictor {
private:
protected:
  AddressPredictor() {}
  Addr_t pcSign(Addr_t pc) const {
    Addr_t cid = pc >> 2;
    cid        = (cid >> 7) ^ (cid);
    return cid;
  }
  uint64_t doHash(Addr_t addr, uint64_t offset) {
    uint64_t sign = (addr << 1) ^ offset;
    return sign;
  }

public:
  static AddressPredictor *create(const char *section, const char *str);

  virtual Addr_t   predict(Addr_t pc, int distance, bool inLine = false)   = 0;
  virtual bool     try_chain_predict(MemObj *dl1, Addr_t pc, int distance) = 0;
  virtual uint16_t exe_update(Addr_t pc, Addr_t addr, Data_t data = 0)     = 0;
  virtual uint16_t ret_update(Addr_t pc, Addr_t addr, Data_t data = 0)     = 0;
};

/**********************
 *
STRIDE Address Predictor

 **********************/

class BimodalLastEntry {
public:
  BimodalLastEntry() {
    addr   = 0;
    data   = 0;
    delta  = 0;
    conf   = 0;
    delta2 = 0;
    conf2  = 0;
  }
  Data_t   data;
  Addr_t   addr;
  int      delta;
  int      delta2;
  uint16_t conf;
  uint16_t conf2;

  void update(int ndelta);
};

class BimodalStride {
protected:
  const int size;

#ifdef UNLIMITED_BIMODAL
  int get_index(Addr_t pc) const { return pc; };

  HASH_MAP<int, BimodalLastEntry> table;
#else
  int get_index(Addr_t pc) const {
    Addr_t cid = pc >> 2;
    cid        = (cid >> 7) ^ (cid);
    return cid & (size - 1);
  };

  std::vector<BimodalLastEntry> table;
#endif

public:
  BimodalStride(int _size);

  void update(Addr_t pc, Addr_t naddr);
  void update_delta(Addr_t pc, int delta);
  void update_addr(Addr_t pc, Addr_t addr);

  uint16_t get_conf(Addr_t pc) const { return table[get_index(pc)].conf; };

  int get_delta(Addr_t pc) const { return table[get_index(pc)].delta; };

  Addr_t get_addr(Addr_t pc) const { return table[get_index(pc)].addr; };
};

class StrideAddressPredictor : public AddressPredictor {
private:
  BimodalStride bimodal;

public:
  StrideAddressPredictor();

  Addr_t   predict(Addr_t pc, int distance, bool inLine);
  bool     try_chain_predict(MemObj *dl1, Addr_t pc, int distance);
  uint16_t exe_update(Addr_t pc, Addr_t addr, Data_t data);
  uint16_t ret_update(Addr_t pc, Addr_t addr, Data_t data);
};

/*****************************

  VTAGE

 *************************************/

class vtage_gentry {
private:
public:
  int      delta;
  int      conf;
  int      u;
  uint16_t loff;  // Signature per LD in the entry

  Addr_t tag;
  bool   thit;
  bool   hit;

  uint16_t update_conf;
  vtage_gentry() {}

  ~vtage_gentry() {}

  void allocate();
  void select(Addr_t t, int b);
  bool conf_steal();
  void conf_force_steal(int delta);
  void conf_update(int ndelta);
  void reset(Addr_t tag, uint16_t loff, int ndelta);
  bool isHit() const { return hit; }
  bool isTagHit() const { return thit; }
  bool isConfident_prefetch() const { return get_conf() >= 2; }

  int get_conf() const {
    if (!hit)
      return 0;  // bias to taken if nothing is known

    return conf;
  }

  bool conf_weak() const {
    if (!hit)
      return true;

    return (conf == 0 || conf == -1);
  }

  bool conf_high() const {
    if (!hit)
      return false;

    return (abs(2 * conf + 1) >= (1 << VTAGE_CWIDTH) - 1);
  }

  int get_delta() const { return delta; }

  int u_get() const { return u; }

  void u_inc() {
    if (u < (1 << VTAGE_UWIDTH) - 1)
      u++;
  }

  void u_dec() {
    if (u)
      u--;
  }

  void u_clear() { u = 0; }
};

class vtage : public AddressPredictor {
private:
protected:
  struct LastAccess {
    Addr_t  addr;             // May be shorter in reality
    Addr_t  pc;               // Not needed, just debug
    int     last_delta[256];  // Last Delta, not sure if needed
    uint8_t last_delta_pos;   // 8 bits to match last_delta size
    uint8_t last_delta_spec_pos;

    LastAccess() { last_delta_pos = 0; };

    int  get_delta(Addr_t _addr) const { return _addr - addr; };
    void clear_spec() { last_delta_spec_pos = last_delta_pos; }
    void spec_add(int delta) {
      last_delta_spec_pos++;
      last_delta[last_delta_spec_pos] = delta;
    }
    void reset(Addr_t _addr, Addr_t _pc) {
      addr                = _addr;
      pc                  = _pc;
      last_delta_spec_pos = last_delta_pos;
    }
    void add(Addr_t _addr, Addr_t _pc) {
      int ndelta = get_delta(_addr);
      reset(_addr, _pc);

      last_delta_pos++;

      last_delta[last_delta_pos] = ndelta;
      last_delta_spec_pos        = last_delta_pos;
    }
  };

  int get_last_index(Addr_t pc) const { return pcSign(pc) & 1023; };

  LastAccess last[1024];  // FIXME: Use a fix size table (configuration time)

  BimodalStride bimodal;
  Stats_cntr    tagePrefetchHistNum;
  Stats_cntr    tagePrefetchBaseNum;

  int pred_delta;  // overall predicted delta
  int pred_conf;
  int hitpred_delta;
  int altpred_delta;

  int hit_bank;
  int alt_bank;

  int    *m;     // [NHIST + 1]; // history lengths
  int    *TB;    //[NHIST + 1];   // tag width for the different tagged tables
  int    *logg;  // [NHIST + 1];  // log of number entries of the different tagged tables
  Addr_t *GI;    //[NHIST + 1];   // indexes to the different tables are computed only once
  Addr_t *GTAG;  //[NHIST + 1];    // tags for the different tables are computed only once

  Addr_t lastBoundaryPC;  // last PC that fetchBoundary was called
  int    TICK;            // for reset of u counter

  Addr_t index_tracker;

  bool   use_alt_pred;
  int8_t use_alt_on_na;

  void rename(Dinst *dinst);

  uint16_t get_offset(Addr_t pc) const { return (pc & ((1UL << log2fetchwidth) - 1)) >> 2; };
  Addr_t   get_boundary(Addr_t pc) const { return (pc >> log2fetchwidth) << log2fetchwidth; };
  Addr_t   get_pc(Addr_t bpc, uint16_t offset) const { return bpc | (offset << 2); };

  void fetchBoundaryLdOffset(Addr_t pc);
  void fetchBoundaryBegin(Addr_t pc);
  void setVtageIndex(uint16_t offset);

  Addr_t gindex(uint16_t offset, int bank);

  void setVtagePrediction(Addr_t pc);
  void updateVtage(Addr_t pc, int delta, uint16_t loff);

  int            tmp = 0;
  const uint64_t log2fetchwidth;
  const uint64_t nhist;  // num of tagged tables
  vtage_gentry **gtable;

public:
  vtage(uint64_t _bsize, uint64_t _log2fetchwidth, uint64_t _bwidth, uint64_t _nhist);

  ~vtage() {}

  Addr_t   predict(Addr_t pc, int dist, bool inLine);
  bool     try_chain_predict(MemObj *dl1, Addr_t pc, int distance);
  uint16_t exe_update(Addr_t pc, Addr_t addr, Data_t data);
  uint16_t ret_update(Addr_t pc, Addr_t addr, Data_t data);
};

// INDIRECT Address Predictor

class IndirectAddressPredictor : public AddressPredictor {
private:
  bool     chainPredict;
  uint16_t maxPrefetch;

  BimodalStride bimodal;

  void performed(MemObj *DL1, Addr_t pc, Addr_t ld1_addr);

public:
  typedef CallbackMember3<IndirectAddressPredictor, MemObj *, Addr_t, Addr_t, &IndirectAddressPredictor::performed> performedCB;
  IndirectAddressPredictor();

  Addr_t   predict(Addr_t pc, int distance, bool inLine);
  bool     try_chain_predict(MemObj *dl1, Addr_t pc, int distance);
  uint16_t exe_update(Addr_t pc, Addr_t addr, Data_t data);
  uint16_t ret_update(Addr_t pc, Addr_t addr, Data_t data);
};
