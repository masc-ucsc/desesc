// See LICENSE for details.

#pragma once

#include <stdarg.h>
#include <string.h>
#include <strings.h>

#include <string>
#include <string_view>

#include "stats.hpp"
#include "config.hpp"
#include "iassert.hpp"
#include "snippets.hpp"

//-------------------------------------------------------------
#define RRIP_M 4  // max value = 2^M   | 4 | 8   | 16   |
//-------------------------------------------------------------
#define DISTANT_REF  3  // 2^M - 1           | 3 | 7   | 15  |
#define IMM_REF      1  // nearimm<imm<dist  | 1 | 1-6 | 1-14|
#define NEAR_IMM_REF 0  // 0                 | 0 | 0   | 0   |
#define LONG_REF     1  // 2^M - 2           | 1 | 6   | 14  |
//-------------------------------------------------------------

enum ReplacementPolicy { LRU, LRUp, RANDOM, SHIP, PAR, UAR, HAWKEYE };  // SHIP is RRIP with SHIP (ISCA 2010)

#define RRIP_MAX      15
#define RRIP_PREF_MAX 2

template <class State, class Addr_t>
class CacheGeneric {
private:
  static const int32_t STR_BUF_SIZE = 1024;

protected:
  const uint32_t size;
  const uint32_t lineSize;
  const uint32_t addrUnit;  // Addressable unit: for most caches = 1 byte
  const uint32_t assoc;
  const uint32_t log2Assoc;
  const uint64_t log2AddrLs;
  const uint64_t maskAssoc;
  const uint32_t sets;
  const uint32_t maskSets;
  const uint32_t log2Sets;
  const uint32_t numLines;

  const bool xorIndex;

  bool goodInterface;

  Stats_cntr *trackstats[16];
  Stats_cntr *trackerZero;
  Stats_cntr *trackerOne;
  Stats_cntr *trackerTwo;
  Stats_cntr *trackerMore;

  Stats_cntr *trackerUp1;
  Stats_cntr *trackerUp1n;
  Stats_cntr *trackerDown1;
  Stats_cntr *trackerDown2;
  Stats_cntr *trackerDown3;
  Stats_cntr *trackerDown4;
  Stats_cntr *trackerDown1n;
  Stats_cntr *trackerDown2n;
  Stats_cntr *trackerDown3n;
  Stats_cntr *trackerDown4n;

public:
  class CacheLine : public State {
  public:
    bool    recent;  // used by skew cache
    uint8_t rrip;    // used by hawkeye and PAR
    CacheLine(int32_t lineSize) : State(lineSize) {}
    // Pure virtual class defines interface
    //
    // Tag included in state. Accessed through:
    //
    // Addr_t getTag() const;
    // void setTag(Addr_t a);
    // void clearTag();
    //
    //
    // bool isValid() const;
    // void invalidate();
  };

  // findLine returns a cache line that has tag == addr, NULL otherwise
  virtual CacheLine *findLineNoEffectPrivate(Addr_t addr)    = 0;
  virtual CacheLine *findLinePrivate(Addr_t addr, Addr_t pc) = 0;

protected:
  CacheGeneric(uint32_t s, uint32_t a, uint32_t b, uint32_t u, bool xr)
      : size(s)
      , lineSize(b)
      , addrUnit(u)
      , assoc(a)
      , log2Assoc(log2i(a))
      , log2AddrLs(log2i(b / u))
      , maskAssoc(a - 1)
      , sets((s / b) / a)
      , maskSets(sets - 1)
      , log2Sets(log2i(sets))
      , numLines(s / b)
      , xorIndex(xr) {
    // TODO : assoc and sets must be a power of 2
  }

  virtual ~CacheGeneric() {}

  void createStats(const std::string &section, const std::string &name);

public:
  // Do not use this interface, use other create
  static CacheGeneric<State, Addr_t> *create(int32_t size, int32_t assoc, int32_t blksize, int32_t addrUnit, const std::string &pStr,
                                             bool skew, bool xr,
                                             uint32_t shct_size = 13);  // 13 is the optimal size specified in the paper
  static CacheGeneric<State, Addr_t> *create(const std::string &section, const std::string &append, const std::string &format);
  void                                destroy() { delete this; }

  virtual CacheLine *findLine2Replace(Addr_t addr, Addr_t pc, bool prefetch) = 0;

  // TO DELETE if flush from Cache.cpp is cleared.  At least it should have a
  // cleaner interface so that Cache.cpp does not touch the internals.
  //
  // Access the line directly without checking TAG
  virtual CacheLine *getPLine(uint32_t l) = 0;

  // ALL USERS OF THIS CLASS PLEASE READ:
  //
  // readLine and writeLine MUST have the same functionality as findLine. The only
  // difference is that readLine and writeLine update power consumption
  // statistics. So, only use these functions when you want to model a physical
  // read or write operation.

  // Use this is for debug checks. Otherwise, a bad interface can be detected

  CacheLine *findLineDebug(Addr_t addr, Addr_t pc = 0) {
#ifndef NDEBUG
    goodInterface = true;
#endif
    CacheLine *line = findLine(addr);
#ifndef NDEBUG
    goodInterface = false;
#endif
    return line;
  }

  CacheLine *findLineNoEffect(Addr_t addr, Addr_t pc = 0) {
#ifndef NDEBUG
    goodInterface = true;
#endif
    CacheLine *line = findLineNoEffectPrivate(addr);
#ifndef NDEBUG
    goodInterface = false;
#endif
    return line;
  }

  CacheLine *findLine(Addr_t addr, Addr_t pc = 0) { return findLinePrivate(addr, pc); }

  CacheLine *readLine(Addr_t addr, Addr_t pc = 0) {
#ifndef NDEBUG
    goodInterface = true;
#endif
    CacheLine *line = findLine(addr, pc);
#ifndef NDEBUG
    goodInterface = false;
#endif

    return line;
  }

  CacheLine *writeLine(Addr_t addr, Addr_t pc = 0) {
#ifndef NDEBUG
    goodInterface = true;
#endif
    CacheLine *line = findLine(addr, pc);
#ifndef NDEBUG
    goodInterface = false;
#endif

    return line;
  }

  CacheLine *fillLine(Addr_t addr, Addr_t pc) {
    CacheLine *l = findLine2Replace(addr, pc, false);
    I(l);

    l->setTag(calcTag(addr));

    return l;
  }

  CacheLine *fillLine_replace(Addr_t addr, Addr_t &rplcAddr, Addr_t pc) {
    CacheLine *l = findLine2Replace(addr, pc, false);
    I(l);
    rplcAddr = 0;

    Addr_t newTag = calcTag(addr);
    if (l->isValid()) {
      Addr_t curTag = l->getTag();
      if (curTag != newTag) {
        rplcAddr = calcAddr4Tag(curTag);
      }
    }

    l->setTag(newTag);

    return l;
  }

  CacheLine *fillLine_replace(Addr_t addr, Addr_t &rplcAddr, Addr_t pc, bool prefetch) {
    CacheLine *l = findLine2Replace(addr, pc, prefetch);
    I(l);
    rplcAddr = 0;

    Addr_t newTag = calcTag(addr);
    if (l->isValid()) {
      Addr_t curTag = l->getTag();
      if (curTag != newTag) {
        rplcAddr = calcAddr4Tag(curTag);
      }
    }

    l->setTag(newTag);

    return l;
  }

  uint32_t getLineSize() const { return lineSize; }
  uint32_t getAssoc() const { return assoc; }
  uint32_t getLog2AddrLs() const { return log2AddrLs; }
  uint32_t getLog2Assoc() const { return log2Assoc; }
  uint32_t getMaskSets() const { return maskSets; }
  uint32_t getNumLines() const { return numLines; }
  uint32_t getNumSets() const { return sets; }

  Addr_t calcTag(Addr_t addr) const { return (addr >> log2AddrLs); }

  // Addr_t calcSet4Tag(Addr_t tag)     const { return (tag & maskSets);                  }
  // Addr_t calcSet4Addr(Addr_t addr)   const { return calcSet4Tag(calcTag(addr));        }

  // Addr_t calcIndex4Set(Addr_t set) const { return (set << log2Assoc);                }
  // Addr_t calcIndex4Tag(Addr_t tag) const { return calcIndex4Set(calcSet4Tag(tag));   }
  // uint32_t calcIndex4Addr(Addr_t addr) const { return calcIndex4Set(calcSet4Addr(addr)); }
  Addr_t calcIndex4Tag(Addr_t tag) const {
    Addr_t set;
    if (xorIndex) {
      // tag        = tag ^ (tag>>log2Sets);
      tag = tag ^ (tag >> 5) ^ (tag >> log2Sets);
      // Addr_t odd = (tag&1) | ((tag>>2) & 1) | ((tag>>4)&1) | ((tag>>6)&1) | ((tag>>8)&1) | ((tag>>10)&1) | ((tag>>12)&1) |
      // ((tag>>14)&1) | ((tag>>16)&1) | ((tag>>18)&1) | ((tag>>20)&1); // over 20 bit index???
      set = tag & maskSets;
    } else {
      set = tag & maskSets;
    }
    Addr_t index = set << log2Assoc;
    return index;
  }

  Addr_t calcAddr4Tag(Addr_t tag) const { return (tag << log2AddrLs); }
};

template <class State, class Addr_t>
class HawkCache : public CacheGeneric<State, Addr_t> {
  using CacheGeneric<State, Addr_t>::numLines;
  using CacheGeneric<State, Addr_t>::assoc;
  using CacheGeneric<State, Addr_t>::maskAssoc;
  using CacheGeneric<State, Addr_t>::goodInterface;

private:
public:
  typedef typename CacheGeneric<State, Addr_t>::CacheLine Line;

protected:
  Line             *mem;
  Line            **content;
  uint16_t          irand;
  ReplacementPolicy policy;
  // hawkeye
  std::vector<uint8_t> prediction;
  uint32_t             predictionMask;

  std::vector<uint8_t> usageInterval;
  uint32_t             usageIntervalMask;

  std::vector<uint8_t> occupancyVector;
  int                  trackedAddresses_ptr;
  std::vector<Addr_t>  trackedAddresses;

  int occVectIterator;

  int getUsageIntervalHash(Addr_t addr) const {
    addr = addr >> CacheGeneric<State, Addr_t>::log2AddrLs;  // Drop lower bits (line size)
    addr = (addr >> 5) ^ (addr);
    return addr & (numLines - 1);
  }

  int getPredictionHash(Addr_t pc) const {
    pc = pc >> 2;  // psudo-PC works, no need lower 2 bit

    pc = (pc >> 17) ^ (pc);

    return pc & predictionMask;
  };

  friend class CacheGeneric<State, Addr_t>;
  HawkCache(int32_t size, int32_t assoc, int32_t blksize, int32_t addrUnit, const std::string &pStr, bool xr);

  Line *findLineNoEffectPrivate(Addr_t addr);
  Line *findLinePrivate(Addr_t addr, Addr_t pc = 0);

public:
  virtual ~HawkCache() {
    delete[] content;
    delete[] mem;
  }

  // TODO: do an iterator. not this junk!!
  Line *getPLine(uint32_t l) {
    // Lines [l..l+assoc] belong to the same set
    I(l < numLines);
    return content[l];
  }

  Line *findLine2Replace(Addr_t addr, Addr_t pc, bool prefetch);
};

template <class State, class Addr_t>
class CacheAssoc : public CacheGeneric<State, Addr_t> {
  using CacheGeneric<State, Addr_t>::numLines;
  using CacheGeneric<State, Addr_t>::assoc;
  using CacheGeneric<State, Addr_t>::maskAssoc;
  using CacheGeneric<State, Addr_t>::goodInterface;
  using CacheGeneric<State, Addr_t>::trackstats;
  using CacheGeneric<State, Addr_t>::trackerZero;
  using CacheGeneric<State, Addr_t>::trackerOne;
  using CacheGeneric<State, Addr_t>::trackerTwo;
  using CacheGeneric<State, Addr_t>::trackerMore;
  using CacheGeneric<State, Addr_t>::trackerUp1;
  using CacheGeneric<State, Addr_t>::trackerUp1n;
  using CacheGeneric<State, Addr_t>::trackerDown1;
  using CacheGeneric<State, Addr_t>::trackerDown2;
  using CacheGeneric<State, Addr_t>::trackerDown3;
  using CacheGeneric<State, Addr_t>::trackerDown4;
  using CacheGeneric<State, Addr_t>::trackerDown1n;
  using CacheGeneric<State, Addr_t>::trackerDown2n;
  using CacheGeneric<State, Addr_t>::trackerDown3n;
  using CacheGeneric<State, Addr_t>::trackerDown4n;

private:
public:
  typedef typename CacheGeneric<State, Addr_t>::CacheLine Line;

protected:
  Line             *mem;
  Line            **content;
  uint16_t          irand;
  ReplacementPolicy policy;

  struct Tracker {
    int demand_trend;
    int conf;
    Tracker() {
      demand_trend = -1;
      conf         = 0;
    }
    void done(int nDemand) {
      if (demand_trend < 0) {
        demand_trend = nDemand;
      } else if (demand_trend == nDemand) {
        if (conf < 15)
          conf++;
      } else {
        if (conf > 0 && (demand_trend >> 1) != (nDemand >> 1)) {
          conf--;
        }
        demand_trend = (nDemand + demand_trend) / 2;
        if (nDemand && nDemand > demand_trend)
          demand_trend++;
      }
    }
  };

  std::map<Addr_t, Tracker> pc2tracker;

  friend class CacheGeneric<State, Addr_t>;
  CacheAssoc(int32_t size, int32_t assoc, int32_t blksize, int32_t addrUnit, const std::string &pStr, bool xr);

  void adjustRRIP(Line **theSet, Line **setEnd, Line *change_line, uint16_t next_rrip) {
    if ((change_line)->rrip == next_rrip)
      return;

    if ((change_line)->rrip > next_rrip) {
      change_line->rrip = next_rrip;
      Line **l          = setEnd - 1;
      while (l >= theSet) {
        if ((*l)->rrip < change_line->rrip && (*l)->rrip >= next_rrip)
          (*l)->rrip++;
        l--;
      }
    } else {
      change_line->rrip = next_rrip;
      Line **l          = setEnd - 1;
      while (l >= theSet) {
        if ((*l)->rrip > change_line->rrip && (*l)->rrip <= next_rrip)
          (*l)->rrip--;
        l--;
      }
    }
  }

  Line *findLineNoEffectPrivate(Addr_t addr);
  Line *findLinePrivate(Addr_t addr, Addr_t pc = 0);

public:
  virtual ~CacheAssoc() {
    delete[] content;
    delete[] mem;
  }

  // TODO: do an iterator. not this junk!!
  Line *getPLine(uint32_t l) {
    // Lines [l..l+assoc] belong to the same set
    I(l < numLines);
    return content[l];
  }

  Line *findLine2Replace(Addr_t addr, Addr_t pc, bool prefetch);
};

template <class State, class Addr_t>
class CacheDM : public CacheGeneric<State, Addr_t> {
  using CacheGeneric<State, Addr_t>::numLines;
  using CacheGeneric<State, Addr_t>::goodInterface;

private:
public:
  typedef typename CacheGeneric<State, Addr_t>::CacheLine Line;

protected:
  Line  *mem;
  Line **content;

  friend class CacheGeneric<State, Addr_t>;
  CacheDM(int32_t size, int32_t blksize, int32_t addrUnit, const std::string &pStr, bool xr);

  Line *findLineNoEffectPrivate(Addr_t addr);
  Line *findLinePrivate(Addr_t addr, Addr_t pc = 0);

public:
  virtual ~CacheDM() {
    delete[] content;
    delete[] mem;
  };

  // TODO: do an iterator. not this junk!!
  Line *getPLine(uint32_t l) {
    // Lines [l..l+assoc] belong to the same set
    I(l < numLines);
    return content[l];
  }

  Line *findLine2Replace(Addr_t addr, Addr_t pc, bool prefetch);
};

template <class State, class Addr_t>
class CacheDMSkew : public CacheGeneric<State, Addr_t> {
  using CacheGeneric<State, Addr_t>::numLines;
  using CacheGeneric<State, Addr_t>::goodInterface;

private:
public:
  typedef typename CacheGeneric<State, Addr_t>::CacheLine Line;

protected:
  Line  *mem;
  Line **content;

  friend class CacheGeneric<State, Addr_t>;
  CacheDMSkew(int32_t size, int32_t blksize, int32_t addrUnit, const std::string &pStr);

  Line *findLineNoEffectPrivate(Addr_t addr);
  Line *findLinePrivate(Addr_t addr, Addr_t pc = 0);

public:
  virtual ~CacheDMSkew() {
    delete[] content;
    delete[] mem;
  };

  // TODO: do an iterator. not this junk!!
  Line *getPLine(uint32_t l) {
    // Lines [l..l+assoc] belong to the same set
    I(l < numLines);
    return content[l];
  }

  Line *findLine2Replace(Addr_t addr, Addr_t pc, bool prefetch);
};

template <class State, class Addr_t>
class CacheSHIP : public CacheGeneric<State, Addr_t> {
  using CacheGeneric<State, Addr_t>::numLines;
  using CacheGeneric<State, Addr_t>::assoc;
  using CacheGeneric<State, Addr_t>::maskAssoc;
  using CacheGeneric<State, Addr_t>::goodInterface;

private:
public:
  typedef typename CacheGeneric<State, Addr_t>::CacheLine Line;

protected:
  Line             *mem;
  Line            **content;
  uint16_t          irand;
  ReplacementPolicy policy;

  /***** SHIP ******/
  uint8_t *SHCT;  // (2^log2shct) entries
  uint32_t log2shct;
  /*****************/

  friend class CacheGeneric<State, Addr_t>;
  CacheSHIP(int32_t size, int32_t assoc, int32_t blksize, int32_t addrUnit, const std::string &pStr,
            uint32_t shct_size = 13);  // 13 was the optimal size in the paper

  Line *findLineNoEffectPrivate(Addr_t addr);
  Line *findLinePrivate(Addr_t addr, Addr_t pc = 0);

public:
  virtual ~CacheSHIP() {
    delete[] content;
    delete[] mem;
    delete[] SHCT;
  }

  // TODO: do an iterator. not this junk!!
  Line *getPLine(uint32_t l) {
    // Lines [l..l+assoc] belong to the same set
    I(l < numLines);
    return content[l];
  }

  Line *findLine2Replace(Addr_t addr, Addr_t pc, bool prefetch);
};

template <class Addr_t>
class StateGenericShip {
private:
  Addr_t tag;
  /* SHIP */
  uint8_t rrpv;       // One per cache line
  Addr_t  signature;  // One per cache line
  bool    outcome;    // One per cache line
  /* **** */

public:
  virtual ~StateGenericShip() { tag = 0; }

  Addr_t getTag() const { return tag; }
  void   setTag(Addr_t a) {
      I(a);
      tag = a;
  }
  void clearTag() {
    tag = 0;
    initSHIP();
  }
  void initialize(void *c) { clearTag(); }

  void initSHIP() {
    rrpv      = RRIP_M - 1;
    signature = 0;
    outcome   = false;
  }

  Addr_t  getSignature() const { return signature; }
  void    setSignature(Addr_t a) { signature = a; }
  bool    getOutcome() const { return outcome; }
  void    setOutcome(bool a) { outcome = a; }
  uint8_t getRRPV() const { return rrpv; }

  void setRRPV(uint8_t a) {
    rrpv = a;
    if (rrpv > (RRIP_M - 1))
      rrpv = RRIP_M - 1;
    if (rrpv < 0)
      rrpv = 0;
  }

  void incRRPV() {
    if (rrpv < (RRIP_M - 1))
      rrpv++;
  }

  virtual bool isValid() const { return tag; }

  virtual void invalidate() { clearTag(); }

  virtual void dump(const std::string &str) {}
};

template <class Addr_t>
class StateGeneric {
private:
  Addr_t tag;
  bool   prefetch;  // Line brought for prefetch, not used otherwise

  Addr_t  pc;  // For statistic tracking
  Addr_t  sign;
  uint8_t degree;
  int     nDemand;

public:
  virtual ~StateGeneric() { tag = 0; }
  void    setPC(Addr_t _pc) { pc = _pc; }
  Addr_t  getPC() const { return pc; }
  Addr_t  getSign() const { return sign; }
  uint8_t getDegree() const { return degree; }

  bool isPrefetch() const { return prefetch; }
  void clearPrefetch(Addr_t _pc) {
    prefetch = false;
    // pc       = _pc;
    sign   = 0;
    degree = 0;
  }
  void setPrefetch(Addr_t _pc, Addr_t _sign, uint8_t _degree) {
    prefetch = true;
    // pc       = _pc;
    sign   = _sign;
    degree = _degree;
  }

  Addr_t getTag() const { return tag; }
  void   setTag(Addr_t a) {
      I(a);
      tag     = a;
      nDemand = 0;
  }
  void clearTag() {
    tag     = 0;
    nDemand = 0;
  }
  void initialize(void *c) {
    prefetch = false;
    clearTag();
  }

  virtual bool isValid() const { return tag; }

  virtual void invalidate() {
    clearTag();
    pc       = 0;
    sign     = 0;
    degree   = 0;
    prefetch = false;
  }

  virtual void dump(const std::string &str) {}

  int  getnDemand() const { return nDemand; }
  void incnDemand() { nDemand++; }

  Addr_t  getSignature() const { return 0; }
  void    setSignature(Addr_t a) { I(0); }
  bool    getOutcome() const { return 0; }
  void    setOutcome(bool a) { I(0); }
  uint8_t getRRPV() const { return 0; }

  void setRRPV(uint8_t a) { I(0); }

  void incRRPV() { I(0); }
};

inline constexpr std::string_view k_RANDOM  = "random";
inline constexpr std::string_view k_LRU     = "lru";
inline constexpr std::string_view k_LRUp    = "lrup";
inline constexpr std::string_view k_SHIP    = "ship";
inline constexpr std::string_view k_HAWKEYE = "hawkeye";
inline constexpr std::string_view k_PAR     = "par";
inline constexpr std::string_view k_UAR     = "uar";

// UAR: Usage Aware Replacement

// Class CacheGeneric, the combinational logic of Cache
template <class State, class Addr_t>
CacheGeneric<State, Addr_t> *CacheGeneric<State, Addr_t>::create(int32_t size, int32_t assoc, int32_t bsize, int32_t addrUnit,
                                                                 const std::string &pStr, bool skew, bool xr, uint32_t shct_size) {

  if (size / bsize < assoc) {
    Config::add_error(fmt::format("Invalid cache configuration size {}, line {}, assoc {} (increase size, or decrease line)",
                                  size,
                                  bsize,
                                  assoc));
    return nullptr;
  }

  if (skew && shct_size) {
    Config::add_error(fmt::format("Invalid cache configuration size {}, line {}, assoc {} has SHIP enabled ", size, bsize, assoc));
    return nullptr;
  }

  std::string pStr_lc{pStr};
  std::transform(pStr_lc.begin(), pStr_lc.end(), pStr_lc.begin(), [](unsigned char c){ return std::tolower(c); });

  CacheGeneric *cache;
  if (skew) {
    I(assoc == 1);  // Skew cache should be direct map
    cache = new CacheDMSkew<State, Addr_t>(size, bsize, addrUnit, pStr_lc);
  } else if (assoc == 1) {
    // Direct Map cache
    cache = new CacheDM<State, Addr_t>(size, bsize, addrUnit, pStr_lc, xr);
  } else if (size == (assoc * bsize)) {
    // FA
    if (pStr_lc == k_SHIP) {
      cache = new CacheSHIP<State, Addr_t>(size, assoc, bsize, addrUnit, pStr_lc, shct_size);
    } else if (pStr_lc == k_HAWKEYE) {
      cache = new HawkCache<State, Addr_t>(size, assoc, bsize, addrUnit, pStr_lc, xr);
    } else {
      cache = new CacheAssoc<State, Addr_t>(size, assoc, bsize, addrUnit, pStr_lc, xr);
    }
  } else {
    if (pStr_lc == k_SHIP) {
      cache = new CacheSHIP<State, Addr_t>(size, assoc, bsize, addrUnit, pStr_lc, shct_size);
    } else if (pStr_lc == k_HAWKEYE) {
      cache = new HawkCache<State, Addr_t>(size, assoc, bsize, addrUnit, pStr_lc, xr);
    } else {
      cache = new CacheAssoc<State, Addr_t>(size, assoc, bsize, addrUnit, pStr_lc, xr);
    }
  }

  I(cache);
  return cache;
}

template <class State, class Addr_t>
void CacheGeneric<State, Addr_t>::createStats(const std::string &section, const std::string &name) {
  for (int i = 0; i < 16; i++) {
    trackstats[i] = new Stats_cntr(fmt::format("{}_tracker{}", name, i));
  }
  trackerZero = new Stats_cntr(fmt::format("{}_trackerZero", name));
  trackerOne  = new Stats_cntr(fmt::format("{}_trackerOne", name));
  trackerTwo  = new Stats_cntr(fmt::format("{}_trackerTwo", name));
  trackerMore = new Stats_cntr(fmt::format("{}_trackerMore", name));

  trackerUp1    = new Stats_cntr(fmt::format("{}_trackerUp1", name));
  trackerUp1n   = new Stats_cntr(fmt::format("{}_trackerUp1n", name));
  trackerDown1  = new Stats_cntr(fmt::format("{}_trackerDown1", name));
  trackerDown2  = new Stats_cntr(fmt::format("{}_trackerDown2", name));
  trackerDown3  = new Stats_cntr(fmt::format("{}_trackerDown3", name));
  trackerDown4  = new Stats_cntr(fmt::format("{}_trackerDown4", name));
  trackerDown1n = new Stats_cntr(fmt::format("{}_trackerDown1n", name));
  trackerDown2n = new Stats_cntr(fmt::format("{}_trackerDown2n", name));
  trackerDown3n = new Stats_cntr(fmt::format("{}_trackerDown3n", name));
  trackerDown4n = new Stats_cntr(fmt::format("{}_trackerDown4n", name));
#if 0
  int32_t procId = 0;
  if ( name[0] == 'P' && name[1] == '(' ) {
    // This structure is assigned to an specific processor
    const std::string &number = &name[2];
    procId = atoi(number);
  }
#endif
}

template <class State, class Addr_t>
CacheGeneric<State, Addr_t> *CacheGeneric<State, Addr_t>::create(const std::string &section, const std::string &append, const std::string &cache_name) {
  CacheGeneric *cache = 0;

  auto size_sec        = fmt::format("{}_size", append);
  auto line_size_sec   = fmt::format("{}_line_size", append);
  auto addrUnit_sec    = fmt::format("{}_addrUnit", append);
  auto assoc_sec       = fmt::format("{}_assoc", append);
  auto repl_policy_sec = fmt::format("{}_replPolicy", append);
  auto skew_sec        = fmt::format("{}_skew", append);
  auto xor_sec         = fmt::format("{}_xor", append);
  auto ship_sec        = fmt::format("{}_ship_sign_bits", append);

  int32_t s  = Config::get_power2(section, size_sec);
  int32_t a  = Config::get_power2(section, assoc_sec);
  int32_t b  = Config::get_power2(section, line_size_sec, 1, 1024);
  bool    xr = false;
  if (Config::has_entry(section, xor_sec)) {
    xr = Config::get_bool(section, xor_sec);
  }
  // printf("Created %s cache, with size:%d, assoc %d, bsize %d\n",section,s,a,b);
  bool sk = false;
  if (Config::has_entry(section, skew_sec))
    sk = Config::get_bool(section, skew_sec);
  int32_t u = 1;
  if (Config::has_entry(section, addrUnit_sec)) {
    u = Config::get_power2(section, addrUnit_sec, 0, b);
  }

  // C++20 will cleanup this avoiding explicit std::string conversion
  std::vector<std::string> allowed = {std::string(k_RANDOM), std::string(k_LRU), std::string(k_SHIP), std::string(k_LRUp), std::string(k_HAWKEYE), std::string(k_PAR), std::string(k_UAR)};
  auto pStr_lc = Config::get_string(section, repl_policy_sec, allowed);

  // SHIP
  uint32_t shct_size = 0;
  if (pStr_lc == k_SHIP) {
    shct_size = Config::get_integer(section, ship_sec);
  }

  if (Config::has_errors()) {
    cache = new CacheAssoc<State, Addr_t>(2, 1, 1, 1, pStr_lc, xr);
  } else {
    cache = create(s, a, b, u, pStr_lc, sk, xr, shct_size);
  }

  I(cache);

  cache->createStats(section, cache_name);

  return cache;
}

/*********************************************************
 *  CacheAssoc
 *********************************************************/

template <class State, class Addr_t>
CacheAssoc<State, Addr_t>::CacheAssoc(int32_t size, int32_t assoc, int32_t blksize, int32_t addrUnit, const std::string &pStr, bool xr)
    : CacheGeneric<State, Addr_t>(size, assoc, blksize, addrUnit, xr) {
  I(numLines > 0);

  std::string pStr_lc{pStr};
  std::transform(pStr_lc.begin(), pStr_lc.end(), pStr_lc.begin(), [](unsigned char c){ return std::tolower(c); });

  if (pStr_lc == k_RANDOM)
    policy = RANDOM;
  else if (pStr_lc == k_LRU)
    policy = LRU;
  else if (pStr_lc == k_LRUp)
    policy = LRUp;
  else if (pStr_lc == k_PAR)
    policy = PAR;
  else if (pStr_lc == k_UAR)
    policy = UAR;
  else if (pStr_lc == k_HAWKEYE) {
    Config::add_error(fmt::format("Invalid cache policy. HawkCache should be used [{}]", pStr_lc));
  } else {
    Config::add_error(fmt::format("Invalid cache policy [{}]", pStr_lc));
  }

  mem = (Line *)malloc(sizeof(Line) * (numLines + 1));
  ////read
  for (uint32_t i = 0; i < numLines; i++) {
    new (&mem[i]) Line(blksize);
  }
  content = new Line *[numLines + 1];

  for (uint32_t i = 0; i < numLines; i++) {
    mem[i].initialize(this);
    mem[i].invalidate();
    mem[i].rrip = 0;
    content[i]  = &mem[i];
  }

  irand = 0;
}

template <class State, class Addr_t>
typename CacheAssoc<State, Addr_t>::Line *CacheAssoc<State, Addr_t>::findLineNoEffectPrivate(Addr_t addr) {
  Addr_t tag = this->calcTag(addr);

  Line **theSet = &content[this->calcIndex4Tag(tag)];

  // Check most typical case
  if ((*theSet)->getTag() == tag) {
    // JustDirectory can break this I((*theSet)->isValid());
    return *theSet;
  }

  Line **lineHit = 0;
  Line **setEnd  = theSet + assoc;

  // For sure that position 0 is not (short-cut)
  {
    Line **l = theSet + 1;
    while (l < setEnd) {
      if ((*l)->getTag() == tag) {
        lineHit = l;
        break;
      }
      l++;
    }
  }

  if (lineHit == 0)
    return 0;

  return *lineHit;
}

template <class State, class Addr_t>
typename CacheAssoc<State, Addr_t>::Line *CacheAssoc<State, Addr_t>::findLinePrivate(Addr_t addr, Addr_t pc) {
  Addr_t tag = this->calcTag(addr);

  Line **theSet = &content[this->calcIndex4Tag(tag)];
  Line **setEnd = theSet + assoc;

  // Check most typical case
  if ((*theSet)->getTag() == tag) {
    // JustDirectory can break this I((*theSet)->isValid());

    if (policy == PAR || policy == UAR) {
      uint16_t next_rrip = (*theSet)->rrip;
      if (tag) {
        next_rrip = RRIP_MAX;
      } else if ((*theSet)->rrip < RRIP_PREF_MAX) {
        next_rrip = RRIP_PREF_MAX;
      }
      if (policy == UAR) {
        (*theSet)->incnDemand();
        if (next_rrip > 0 && pc2tracker[(*theSet)->getPC()].conf > 8
            && (1 + pc2tracker[(*theSet)->getPC()].demand_trend) < (*theSet)->getnDemand()) {
          trackerDown3->inc();
          next_rrip /= 2;
        } else {
          trackerDown3n->inc();
        }
      }

      adjustRRIP(theSet, setEnd, *theSet, next_rrip);
    }

    return *theSet;
  }

  Line **lineHit = 0;

  {
    Line **l = theSet + 1;  // +1 because 0 is already checked
    while (l < setEnd) {
      if ((*l)->getTag() == tag) {
        lineHit = l;
        break;
      }
      l++;
    }
  }

  if (lineHit == 0) {
    return 0;
  }

  I((*lineHit)->isValid());

  // No matter what is the policy, move lineHit to the *theSet. This
  // increases locality
  Line *tmp = *lineHit;
  {
    Line **l = lineHit;
    while (l > theSet) {
      Line **prev = l - 1;
      *l          = *prev;
      l           = prev;
    }
    *theSet = tmp;
  }

  uint16_t next_rrip = tmp->rrip;
  if (tag) {
    next_rrip = RRIP_MAX;
  } else if ((tmp)->rrip < RRIP_PREF_MAX) {
    next_rrip = RRIP_PREF_MAX;
  }
  if (policy == UAR) {
    tmp->incnDemand();
    if (pc2tracker[tmp->getPC()].conf > 8 && (1 + pc2tracker[tmp->getPC()].demand_trend) < tmp->getnDemand() && next_rrip > 0) {
      trackerDown4->inc();
      next_rrip /= 2;
    } else {
      trackerDown4n->inc();
    }
  }
  adjustRRIP(theSet, setEnd, tmp, next_rrip);

#if 0
  if (policy == UAR) {
    printf("read %d ", this->calcIndex4Tag(tag));
    Line **l = setEnd -1;
    int conta = 0;
    while(l >= theSet) {
      printf(" %d:%d", conta,(*l)->rrip);
      l--;
      conta++;
    }
    printf("\n");
  }
#endif

  return tmp;
}

template <class State, class Addr_t>
typename CacheAssoc<State, Addr_t>::Line *CacheAssoc<State, Addr_t>::findLine2Replace(Addr_t addr, Addr_t pc, bool prefetch) {
  Addr_t tag = this->calcTag(addr);
  I(tag);
  Line **theSet = &content[this->calcIndex4Tag(tag)];
  Line **setEnd = theSet + assoc;

#if 0
  // OK for cache, not BTB
  assert((*theSet)->getTag() != tag);
#else
  if ((*theSet)->getTag() == tag) {
    GI(tag, (*theSet)->isValid());

    if (policy == PAR || policy == UAR) {
      if (prefetch)
        I(tag == 0);

      uint16_t next_rrip = (*theSet)->rrip;
      if (tag) {
        next_rrip = RRIP_MAX;
      } else if ((*theSet)->rrip < RRIP_PREF_MAX) {
        if (prefetch) {
          next_rrip = RRIP_PREF_MAX;
        } else
          next_rrip = RRIP_MAX;
      }
      adjustRRIP(theSet, setEnd, *theSet, next_rrip);
    }

    return *theSet;
  }
#endif

  Line **lineHit  = 0;
  Line **lineFree = 0;  // Order of preference, invalid

  {
    Line **l = setEnd - 1;
    while (l >= theSet) {
      if ((*l)->getTag() == tag) {
        lineHit = l;
        break;
      }
      if (!(*l)->isValid())
        lineFree = l;
      else if (lineFree == 0)
        lineFree = l;
      else if ((*l)->rrip < (*lineFree)->rrip)  // == too to add a bit of LRU order between same RIPs
        lineFree = l;
      else if (policy == UAR && ((*l)->rrip == (*lineFree)->rrip)) {
        if (pc2tracker[(*l)->getPC()].demand_trend < pc2tracker[(*lineFree)->getPC()].demand_trend)
          lineFree = l;
      }

      l--;
    }
  }

  Line  *tmp;
  Line **tmp_pos;
  if (!lineHit) {
    I(lineFree);

    if (policy == RANDOM) {
      lineFree = &theSet[irand];
      irand    = (irand + 1) & maskAssoc;
      if (irand == 0)
        irand = (irand + 1) & maskAssoc;  // Not MRU
    } else {
      I(policy == LRU || policy == LRUp || policy == PAR || policy == UAR);
      // Get the oldest line possible
      lineFree = setEnd - 1;
    }

    I(lineFree);

    if (lineFree == theSet && policy != PAR && policy != UAR)
      return *lineFree;  // Hit in the first possition

    tmp     = *lineFree;
    tmp_pos = lineFree;
  } else {
    tmp     = *lineHit;
    tmp_pos = lineHit;
  }

  if (tmp->isValid() && policy == UAR) {
    pc2tracker[tmp->getPC()].done(tmp->getnDemand());
    if (tmp->getnDemand() == 0) {
      trackerZero->inc();
    } else if (tmp->getnDemand() == 1) {
      trackerOne->inc();
    } else if (tmp->getnDemand() == 2) {
      trackerTwo->inc();
    } else {
      trackerMore->inc();
    }

    trackstats[pc2tracker[tmp->getPC()].conf]->inc();
  }
  tmp->setPC(pc);

  if (prefetch) {
    if (policy == PAR) {
      adjustRRIP(theSet, setEnd, tmp, 0);
    } else if (policy == UAR) {
      uint16_t default_rrip_prefetch = 0;
      if (pc2tracker[pc].conf > 0 && pc2tracker[pc].demand_trend > 0) {
        default_rrip_prefetch = RRIP_MAX / 2;
        trackerUp1->inc();
      } else {
        trackerUp1n->inc();
      }
      adjustRRIP(theSet, setEnd, tmp, default_rrip_prefetch);
    }
    return tmp;
  }

  if (policy == LRUp) {
  } else if (policy == PAR || policy == UAR) {
    uint16_t default_rrip = RRIP_MAX;
    if (policy == UAR) {
      if (pc2tracker[pc].conf > 8 && pc2tracker[pc].demand_trend == 0) {
        default_rrip = 0;
        trackerDown1->inc();
      } else if (pc2tracker[pc].conf > 8 && pc2tracker[pc].demand_trend == 1) {
        default_rrip /= 2;
        trackerDown2->inc();
      } else {
        trackerDown2n->inc();
      }
    }
    adjustRRIP(theSet, setEnd, tmp, default_rrip);

    Line **l = tmp_pos;
    while (l > theSet) {
      Line **prev = l - 1;
      *l          = *prev;
      ;
      l = prev;
    }
    *theSet = tmp;
  } else {
    Line **l = tmp_pos;
    while (l > theSet) {
      Line **prev = l - 1;
      *l          = *prev;
      ;
      l = prev;
    }
    *theSet = tmp;
  }

  // tmp->rrip = RRIP_MAX;

#if 0
  if (policy == UAR) {
    printf("repl %d ", this->calcIndex4Tag(tag));
    Line **l = setEnd -1;
    int conta = 0;
    while(l >= theSet) {
      printf(" %d:%d", conta,(*l)->rrip);
      l--;
      conta++;
    }
    printf("\n");
  }
#endif

  return tmp;
}
/*********************************************************
 *  HawkCache
 *********************************************************/

template <class State, class Addr_t>
HawkCache<State, Addr_t>::HawkCache(int32_t size, int32_t assoc, int32_t blksize, int32_t addrUnit, const std::string &pStr, bool xr)
    : CacheGeneric<State, Addr_t>(size, assoc, blksize, addrUnit, xr) {
  I(numLines > 0);

  std::string pStr_lc{pStr};
  std::transform(pStr_lc.begin(), pStr_lc.end(), pStr_lc.begin(), [](unsigned char c){ return std::tolower(c); });

  if (pStr_lc != k_HAWKEYE) {
    Config::add_error(fmt::format("Invalid cache policy. Cache should be used [{}]", pStr_lc));
  }

  mem = (Line *)malloc(sizeof(Line) * (numLines + 1));
  ////read
  for (uint32_t i = 0; i < numLines; i++) {
    new (&mem[i]) Line(blksize);
  }
  content = new Line *[numLines + 1];

  for (uint32_t i = 0; i < numLines; i++) {
    mem[i].initialize(this);
    mem[i].invalidate();
    content[i] = &mem[i];
  }

  irand = 0;

  // hawkeye
  prediction.resize(512);
  predictionMask = prediction.size() - 1;

  usageInterval.resize(8192);
  usageIntervalMask = usageInterval.size() - 1;

  trackedAddresses.resize(8192);
  trackedAddresses_ptr = 0;

  occupancyVector.resize(8192);
  occVectIterator = 0;
}

template <class State, class Addr_t>
typename HawkCache<State, Addr_t>::Line *HawkCache<State, Addr_t>::findLineNoEffectPrivate(Addr_t addr) {
  Addr_t tag = this->calcTag(addr);

  Line **theSet = &content[this->calcIndex4Tag(tag)];

  // Check most typical case
  if ((*theSet)->getTag() == tag) {
    // JustDirectory can break this I((*theSet)->isValid());
    return *theSet;
  }

  Line **lineHit = 0;
  Line **setEnd  = theSet + assoc;

  // For sure that position 0 is not (short-cut)
  {
    Line **l = theSet + 1;
    while (l < setEnd) {
      if ((*l)->getTag() == tag) {
        lineHit = l;
        break;
      }
      l++;
    }
  }

  if (lineHit == 0)
    return 0;

  return *lineHit;
}

template <class State, class Addr_t>
typename HawkCache<State, Addr_t>::Line *HawkCache<State, Addr_t>::findLinePrivate(Addr_t addr, Addr_t pc) {
  Addr_t tag = this->calcTag(addr);

  Line **theSet = &content[this->calcIndex4Tag(tag)];

  // Check most typical case
  if ((*theSet)->getTag() == tag) {
    // JustDirectory can break this I((*theSet)->isValid());
    return *theSet;
  }

  Line **lineHit = 0;
  Line **setEnd  = theSet + assoc;

  // For sure that position 0 is not (short-cut)
  {
    Line **l = theSet + 1;
    while (l < setEnd) {
      if ((*l)->getTag() == tag) {
        lineHit = l;
        break;
      }
      l++;
    }
  }

  // hawkeye
  uint8_t OPToutcome  = 0;
  uint8_t missFlag    = 0;
  int     occVectSize = 8192;
  int     trAddSize   = 8192;
  int     curAddr     = 0;

  int hashAddr = getUsageIntervalHash(addr);

  int predHPC = getPredictionHash(pc);

  // OPTgen -----------------
  occVectIterator++;
  if (occVectIterator >= occVectSize) {
    occVectIterator = 0;
    for (int i = 0; i < occVectSize; i++) {
      occupancyVector[i] = 0;
    }
  }
  for (int i = 0; i < trAddSize; i++) {  // Update usage interval for all tracked addresses
    if (trackedAddresses[i] == 0)
      continue;
    if (trackedAddresses[i] == tag) {
      curAddr = i;
    }

    if (usageInterval[getUsageIntervalHash(trackedAddresses[i])] < 255)
      usageInterval[getUsageIntervalHash(trackedAddresses[i])]++;
  }

  if (usageInterval[hashAddr] > 0) {  // Address has been loaded before
    int limit = occVectIterator - usageInterval[hashAddr];
    if (limit < 0) {
      limit = 0;
    } else if (limit >= occVectSize) {
      limit = occVectSize - 1;
    }
    for (int i = limit; i < occVectIterator; i++) {
      if (occupancyVector[i] >= numLines) {
        missFlag = 1;
        break;  // If cache is full during usage interval, then it will miss
      }
    }

    // If it doesn't miss, increase occupancy vector during liveness interval
    if (missFlag != 1) {
      for (int i = limit; i < occVectIterator; i++) {
        if (occupancyVector[i] < 255)
          occupancyVector[i]++;
      }
      usageInterval[hashAddr]   = 0;
      trackedAddresses[curAddr] = 0;
    }
  } else {  // Current address hasn't been loaded yet
    usageInterval[hashAddr] = 1;

    // Start tracking current address;
    bool found = false;
    for (int i = 0; i < trAddSize; i++) {
      if (trackedAddresses[i] == tag || trackedAddresses[i] == 0) {
        found               = true;
        trackedAddresses[i] = tag;
        break;
      }
    }
    if (!found) {
      trackedAddresses[trackedAddresses_ptr++] = tag;
      if (trackedAddresses_ptr >= trackedAddresses.size())
        trackedAddresses_ptr = 0;
    }
  }

  // 0 means it predicts a miss, 1 means it predicts a hit
  if (missFlag == 1) {
    OPToutcome = 0;
  } else {
    OPToutcome = 1;
  }

  // Hawkeye prediction ---------------
  if (OPToutcome == 1) {
    if (prediction[predHPC] < 7) {
      prediction[predHPC]++;
    }
  } else {
    if (prediction[predHPC] > 0) {
      prediction[predHPC]--;
    }
  }

  // A prediction of 0 is cache-averse, 1 is friendly
  uint8_t hawkPrediction = prediction[predHPC] >> 2;

  if (lineHit == 0) {
    if (hawkPrediction == 1) {  // if predict friendly and cache miss, age all lines;
      Line **l = theSet + 1;
      while (l < setEnd) {
        if ((*l)->isValid() && (*l)->rrip < 6) {
          (*l)->rrip++;
        }
        l++;
      }
    }
    return 0;
  }

  if ((*lineHit)->isValid()) {
    // Setting RRIPs
    if (hawkPrediction == 0) {
      (*lineHit)->rrip = 7;
    } else {
      (*lineHit)->rrip = 0;
    }
  }

  I((*lineHit)->isValid());

  return *lineHit;
}

template <class State, class Addr_t>
typename HawkCache<State, Addr_t>::Line *HawkCache<State, Addr_t>::findLine2Replace(Addr_t addr, Addr_t pc, bool prefetch) {
  Addr_t tag = this->calcTag(addr);
  I(tag);
  Line **theSet = &content[this->calcIndex4Tag(tag)];

  // Check most typical case
  if ((*theSet)->getTag() == tag) {
    GI(tag, (*theSet)->isValid());
    return *theSet;
  }

  Line **lineHit  = 0;
  Line **lineFree = 0;  // Order of preference, invalid
  Line **setEnd   = theSet + assoc;

  {
    Line **l = setEnd - 1;
    while (l >= theSet) {
      if ((*l)->getTag() == tag) {
        lineHit = l;
        break;
      }
      if (!(*l)->isValid())
        lineFree = l;
      else if (lineFree == 0)
        lineFree = l;

      l--;
    }
  }

  if (lineHit) {
    return *lineHit;
  }

  I(lineFree);

  Line **l   = setEnd - 1;
  Line **tmp = lineFree;
  // iterate through each cacheline and free the first one with rrip of 7
  while (l >= theSet) {
    if ((*l)->isValid() && (*l)->rrip == 7) {
      lineFree = l;
      break;
    }
    l--;
  }

  // if no lines have rrip == 7, revert to LRU and detrain hawk prediction
  if (lineFree == tmp && (*lineFree)->rrip != 7) {
    lineFree = setEnd - 1;
    if (prediction[getPredictionHash(pc)] > 0)
      prediction[getPredictionHash(pc)]--;
  }

  I(lineFree);

  return *lineFree;
}

/*********************************************************
 *  CacheDM
 *********************************************************/

template <class State, class Addr_t>
CacheDM<State, Addr_t>::CacheDM(int32_t size, int32_t blksize, int32_t addrUnit, const std::string &pStr, bool xr)
    : CacheGeneric<State, Addr_t>(size, 1, blksize, addrUnit, xr) {
  I(numLines > 0);

  mem = (Line *)malloc(sizeof(Line) * (numLines + 1));
  for (uint32_t i = 0; i < numLines; i++) {
    new (&mem[i]) Line(blksize);
  }
  content = new Line *[numLines + 1];

  for (uint32_t i = 0; i < numLines; i++) {
    mem[i].initialize(this);
    mem[i].invalidate();
    content[i] = &mem[i];
  }
}

template <class State, class Addr_t>
typename CacheDM<State, Addr_t>::Line *CacheDM<State, Addr_t>::findLineNoEffectPrivate(Addr_t addr) {
  Addr_t tag = this->calcTag(addr);
  I(tag);

  Line *line = content[this->calcIndex4Tag(tag)];

  if (line->getTag() == tag) {
    I(line->isValid());
    return line;
  }

  return 0;
}

template <class State, class Addr_t>
typename CacheDM<State, Addr_t>::Line *CacheDM<State, Addr_t>::findLinePrivate(Addr_t addr, Addr_t pc) {
  return findLineNoEffectPrivate(addr);
}

template <class State, class Addr_t>
typename CacheDM<State, Addr_t>::Line *CacheDM<State, Addr_t>::findLine2Replace(Addr_t addr, Addr_t pc, bool prefetch) {
  Addr_t tag  = this->calcTag(addr);
  Line  *line = content[this->calcIndex4Tag(tag)];

  return line;
}

/*********************************************************
 *  CacheDMSkew
 *********************************************************/

template <class State, class Addr_t>
CacheDMSkew<State, Addr_t>::CacheDMSkew(int32_t size, int32_t blksize, int32_t addrUnit, const std::string &pStr)
    : CacheGeneric<State, Addr_t>(size, 1, blksize, addrUnit, false) {
  I(numLines > 0);

  mem = (Line *)malloc(sizeof(Line) * (numLines + 1));
  for (uint32_t i = 0; i < numLines; i++) {
    new (&mem[i]) Line(blksize);
  }
  content = new Line *[numLines + 1];

  for (uint32_t i = 0; i < numLines; i++) {
    mem[i].initialize(this);
    mem[i].invalidate();
    content[i] = &mem[i];
  }
}

template <class State, class Addr_t>
typename CacheDMSkew<State, Addr_t>::Line *CacheDMSkew<State, Addr_t>::findLineNoEffectPrivate(Addr_t addr) {
  Addr_t tag1 = this->calcTag(addr);
  I(tag1);

  Line *line = content[this->calcIndex4Tag(tag1)];

  if (line->getTag() == tag1) {
    I(line->isValid());
    line->recent = true;
    return line;
  }
  Line *line0 = line;

  // BEGIN Skew cache
  // Addr_t tag2 = (tag1 ^ (tag1>>1));
  Addr_t addrh = (addr >> 5) ^ (addr >> 11);
  Addr_t tag2  = this->calcTag(addrh);
  I(tag2);
  line = content[this->calcIndex4Tag(tag2)];

  if (line->getTag() == tag1) {  // FIRST TAG, tag2 is JUST used for indexing the table
    I(line->isValid());
    line->recent = true;
    return line;
  }
  Line *line1 = line;

#if 1
  // Addr_t tag3 = (tag1 ^ ((tag1>>1) + ((tag1 & 0xFFFF))));
  addrh       = addrh + (addr & 0xFF);
  Addr_t tag3 = this->calcTag(addrh);
  I(tag3);
  line = content[this->calcIndex4Tag(tag3)];

  if (line->getTag() == tag1) {  // FIRST TAG, tag2 is JUST used for indexing the table
    I(line->isValid());
    line->recent = true;
    return line;
  }
  Line *line3 = line;

  line3->recent = false;
#endif
  line1->recent = false;
  line0->recent = false;

  return 0;
}

template <class State, class Addr_t>
typename CacheDMSkew<State, Addr_t>::Line *CacheDMSkew<State, Addr_t>::findLinePrivate(Addr_t addr, Addr_t pc) {
  return findLineNoEffectPrivate(addr);
}

template <class State, class Addr_t>
typename CacheDMSkew<State, Addr_t>::Line *CacheDMSkew<State, Addr_t>::findLine2Replace(Addr_t addr, Addr_t pc, bool prefetch) {
  Addr_t tag1  = this->calcTag(addr);
  Line  *line1 = content[this->calcIndex4Tag(tag1)];

  if (line1->getTag() == tag1) {
    GI(tag1, line1->isValid());
    return line1;
  }

  // BEGIN Skew cache
  // Addr_t tag2 = (tag1 ^ (tag1>>1));
  Addr_t addrh = (addr >> 5) ^ (addr >> 11);
  Addr_t tag2  = this->calcTag(addrh);
  Line  *line2 = content[this->calcIndex4Tag(tag2)];

  if (line2->getTag() == tag1) {  // FIRST TAG, tag2 is JUST used for indexing the table
    I(line2->isValid());
    return line2;
  }

  static int32_t rand_number = 0;
  rand_number++;

#if 1
  // Addr_t tag3 = (tag1 ^ ((tag1>>1) + ((tag1 & 0xFFFF))));
  addrh        = addrh + (addr & 0xFF);
  Addr_t tag3  = this->calcTag(addrh);
  Line  *line3 = content[this->calcIndex4Tag(tag3)];

  if (line3->getTag() == tag1) {  // FIRST TAG, tag2 is JUST used for indexing the table
    I(line3->isValid());
    return line3;
  }

  while (1) {
    if (rand_number > 2)
      rand_number = 0;

    if (rand_number == 0) {
      if (line1->recent)
        line1->recent = false;
      else {
        line1->recent = true;
        line2->recent = false;
        line3->recent = false;
        return line1;
      }
    }
    if (rand_number == 1) {
      if (line2->recent)
        line2->recent = false;
      else {
        line1->recent = false;
        line2->recent = true;
        line3->recent = false;
        return line2;
      }
    } else {
      if (line3->recent)
        line3->recent = false;
      else {
        line1->recent = false;
        line2->recent = false;
        line3->recent = true;
        return line3;
      }
    }
  }
#else
  if ((rand_number & 1) == 0)
    return line1;
  return line2;
#endif
  // END Skew cache
}

/*********************************************************
 *  CacheSHIP
 *********************************************************/

template <class State, class Addr_t>
CacheSHIP<State, Addr_t>::CacheSHIP(int32_t size, int32_t assoc, int32_t blksize, int32_t addrUnit, const std::string &pStr,
                                    uint32_t shct_size)
    : CacheGeneric<State, Addr_t>(size, assoc, blksize, addrUnit, false) {
  I(numLines > 0);
  log2shct = shct_size;
  I(shct_size < 31);
  SHCT = new uint8_t[(2 << log2shct)];

  std::string pStr_lc{pStr};
  std::transform(pStr_lc.begin(), pStr_lc.end(), pStr_lc.begin(), [](unsigned char c){ return std::tolower(c); });

  if (pStr_lc == k_SHIP) {
    policy = SHIP;
  } else {
    Config::add_error(fmt::format("Invalid cache policy [{}]", pStr));
  }

  mem = (Line *)malloc(sizeof(Line) * (numLines + 1));
  for (uint32_t i = 0; i < numLines; i++) {
    new (&mem[i]) Line(blksize);
  }
  content = new Line *[numLines + 1];

  for (uint32_t i = 0; i < numLines; i++) {
    mem[i].initialize(this);
    mem[i].invalidate();
    content[i] = &mem[i];
  }

  for (uint32_t j = 0; j < (2 ^ log2shct); j++) {
    SHCT[j] = 0;
  }

  irand = 0;
}

template <class State, class Addr_t>
typename CacheSHIP<State, Addr_t>::Line *CacheSHIP<State, Addr_t>::findLineNoEffectPrivate(Addr_t addr) {
  Addr_t tag     = this->calcTag(addr);
  Line **theSet  = &content[this->calcIndex4Tag(tag)];
  Line **setEnd  = theSet + assoc;
  Line **lineHit = 0;

  {
    Line **l = theSet;
    while (l < setEnd) {
      if ((*l)->getTag() == tag) {
        lineHit = l;
        break;
      }
      l++;
    }
  }

  return *lineHit;
}

template <class State, class Addr_t>
typename CacheSHIP<State, Addr_t>::Line *CacheSHIP<State, Addr_t>::findLinePrivate(Addr_t addr, Addr_t pc) {
  Addr_t tag     = this->calcTag(addr);
  Line **theSet  = &content[this->calcIndex4Tag(tag)];
  Line **setEnd  = theSet + assoc;
  Line **lineHit = 0;

  {
    Line **l = theSet;
    while (l < setEnd) {
      if ((*l)->getTag() == tag) {
        lineHit = l;
        break;
      }
      l++;
    }
  }

  if (lineHit == 0) {
    // Cache MISS
    return 0;
  }

  I((*lineHit)->isValid());
  // Cache HIT
  if (true) {
    // pc is the PC or the memory address, not the usable signature
    Addr_t signature = (pc ^ ((pc >> 1) + ((pc & 0xFFFFFFFF))));
    signature &= ((2 << log2shct) - 1);

    (*lineHit)->setOutcome(true);

    if (SHCT[signature] < 7)  // 3 bit counter. But why? Whatabout 2^log2Assoc - 1
      SHCT[signature] += 1;
  }

  (*lineHit)->setRRPV(0);
  Line *tmp = *lineHit;

  /*
     // No matter what is the policy, move lineHit to the *theSet. This
     // increases locality
     {
      Line **l = lineHit;
      while(l > theSet) {
        Line **prev = l - 1;
        *l = *prev;;
        l = prev;
      }
      *theSet = tmp;
     }
  */
  return tmp;
}

template <class State, class Addr_t>
typename CacheSHIP<State, Addr_t>::Line *CacheSHIP<State, Addr_t>::findLine2Replace(Addr_t addr, Addr_t pc, bool prefetch) {
  Addr_t tag = this->calcTag(addr);
  I(tag);

  Line **theSet = &content[this->calcIndex4Tag(tag)];
  Line **setEnd = theSet + assoc;

  Line **lineFree = 0;  // Order of preference, invalid, rrpv = 3
  Line **lineHit  = 0;  // Exact tag match

  {
    Line **l = theSet;
    do {
      l = theSet;

      while (l < setEnd) {
        // Tag matches
        if ((*l)->getTag() == tag) {
          lineHit = l;
          break;
        }

        // Line is invalid
        if (!(*l)->isValid()) {
          lineFree = l;
          break;
        }

        // Line is distant re-referenced
        if ((*l)->getRRPV() >= RRIP_M - 1) {
          lineFree = l;
          break;
        }

        l++;
      }

      if ((lineHit == 0) && (lineFree == 0)) {
        l = theSet;
        while (l < setEnd) {
          (*l)->incRRPV();
          l++;
        }
      }
    } while ((lineHit == 0) && (lineFree == 0));
  }

  Line *tmp;
  if (!lineHit) {
    tmp = *lineFree;
  } else {
    tmp = *lineHit;
  }

  // updateSHIP = false;
  if (true) {
    // pc is the PC or the memory address, not the usable signature
    Addr_t signature = (pc ^ ((pc >> 1) + ((pc & 0xFFFFFFFF))));
    signature &= ((2 << log2shct) - 1);

    if ((tmp->isValid())) {
      Addr_t signature_m = (tmp)->getSignature();
      if ((tmp)->getOutcome() == false) {
        if (SHCT[signature_m] > 0) {
          SHCT[signature_m] -= 1;
        }
      }
    }

    (tmp)->setOutcome(false);
    (tmp)->setSignature(signature);
    if (SHCT[signature] == 0) {
      (tmp)->setRRPV(RRIP_M - 1);
    } else {
      (tmp)->setRRPV(RRIP_M / 2);
    }
  } else {
    (tmp)->setRRPV(RRIP_M / 2);
  }

  /*
    {
      Line **l = tmp_pos;
      while(l > theSet) {
        Line **prev = l - 1;
        *l = *prev;;
        l = prev;
      }
      *theSet = tmp;
    }
  */
  return tmp;
}
