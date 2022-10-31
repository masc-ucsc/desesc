// See LICENSE for details.

#include "fmt/format.h"

#include "iassert.hpp"
#include "snippets.hpp"

//-------------------------------------------------------------
#define RRIP_M 4 // max value = 2^M   | 4 | 8   | 16   |
//-------------------------------------------------------------
#define DISTANT_REF 3  // 2^M - 1           | 3 | 7   | 15  |
#define IMM_REF 1      // nearimm<imm<dist  | 1 | 1-6 | 1-14|
#define NEAR_IMM_REF 0 // 0                 | 0 | 0   | 0   |
#define LONG_REF 1     // 2^M - 2           | 1 | 6   | 14  |
//-------------------------------------------------------------

enum ReplacementPolicy { LRU, LRUp, RANDOM, SHIP }; // SHIP is RRIP with SHIP (ISCA 2010)

template <class State, class Addr_t> class CacheGeneric {
private:
  static const int32_t STR_BUF_SIZE = 1024;

protected:
  const uint32_t size;
  const uint32_t lineSize;
  const uint32_t addrUnit; // Addressable unit: for most caches = 1 byte
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

public:
  class CacheLine : public State {
  public:
    bool recent; // used by skew cache
    CacheLine(int32_t lineSize)
        : State(lineSize) {
    }
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
  virtual CacheLine *findLinePrivate(Addr_t addr, bool updateSHIP, Addr_t SHIP_signature) = 0;

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

  virtual ~CacheGeneric() {
  }

  void createStats(const char *section, const char *name);

public:
  // Do not use this interface, use other create
  static CacheGeneric<State, Addr_t> *create(int32_t size, int32_t assoc, int32_t blksize, int32_t addrUnit, const char *pStr,
                                             bool skew, bool xr,
                                             uint32_t shct_size = 13); // 13 is the optimal size specified in the paper
  static CacheGeneric<State, Addr_t> *create(const char *section, const char *append, const char *format, ...);
  void                                destroy() {
    delete this;
  }

  virtual CacheLine *findLine2Replace(Addr_t addr, bool updateSHIP, Addr_t SHIP_signature) = 0;

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

  CacheLine *findLineDebug(Addr_t addr, bool updateSHIP = false, Addr_t SHIP_signature = 0) {
    (void)updateSHIP;
    (void)SHIP_signature;

    IS(goodInterface = true);
    CacheLine *line = findLine(addr); // SHIP stats will not be updated
    IS(goodInterface = false);

    return line;
  }

  CacheLine *findLineNoEffect(Addr_t addr, bool updateSHIP = false, Addr_t SHIP_signature = 0) {
    (void)updateSHIP;
    (void)SHIP_signature;

    IS(goodInterface = true);
    CacheLine *line = findLine(addr); // SHIP stats will not be updated
    IS(goodInterface = false);

    return line;
  }

  CacheLine *findLine(Addr_t addr, bool updateSHIP = false, Addr_t SHIP_signature = 0) {
    return findLinePrivate(addr, updateSHIP, SHIP_signature);
  }

  CacheLine *readLine(Addr_t addr, bool updateSHIP = false, Addr_t SHIP_signature = 0) {
    (void)updateSHIP;
    (void)SHIP_signature;

    IS(goodInterface = true);
    CacheLine *line = findLine(addr, updateSHIP, SHIP_signature);
    IS(goodInterface = false);

    return line;
  }

  CacheLine *writeLine(Addr_t addr, bool updateSHIP = false, Addr_t SHIP_signature = 0) {
    (void)updateSHIP;
    (void)SHIP_signature;

    IS(goodInterface = true);
    CacheLine *line = findLine(addr, updateSHIP, SHIP_signature);
    IS(goodInterface = false);

    return line;
  }

  CacheLine *fillLine(Addr_t addr, bool updateSHIP = false, Addr_t SHIP_signature = 0) {

    CacheLine *l = findLine2Replace(addr, updateSHIP, SHIP_signature);
    I(l);
    l->setTag(calcTag(addr));

    return l;
  }

  CacheLine *fillLine(Addr_t addr, Addr_t &rplcAddr, bool updateSHIP = false, Addr_t SHIP_signature = 0) {
    CacheLine *l = findLine2Replace(addr, updateSHIP, SHIP_signature);
    I(l);
    rplcAddr = 0;

    Addr_t newTag = calcTag(addr);
    if(l->isValid()) {
      Addr_t curTag = l->getTag();
      if(curTag != newTag) {
        rplcAddr = calcAddr4Tag(curTag);
      }
    }

    l->setTag(newTag);

    return l;
  }

  uint32_t getLineSize() const {
    return lineSize;
  }
  uint32_t getAssoc() const {
    return assoc;
  }
  uint32_t getLog2AddrLs() const {
    return log2AddrLs;
  }
  uint32_t getLog2Assoc() const {
    return log2Assoc;
  }
  uint32_t getMaskSets() const {
    return maskSets;
  }
  uint32_t getNumLines() const {
    return numLines;
  }
  uint32_t getNumSets() const {
    return sets;
  }

  Addr_t calcTag(Addr_t addr) const {
    return (addr >> log2AddrLs);
  }

  // Addr_t calcSet4Tag(Addr_t tag)     const { return (tag & maskSets);                  }
  // Addr_t calcSet4Addr(Addr_t addr)   const { return calcSet4Tag(calcTag(addr));        }

  // Addr_t calcIndex4Set(Addr_t set) const { return (set << log2Assoc);                }
  // Addr_t calcIndex4Tag(Addr_t tag) const { return calcIndex4Set(calcSet4Tag(tag));   }
  // uint32_t calcIndex4Addr(Addr_t addr) const { return calcIndex4Set(calcSet4Addr(addr)); }
  Addr_t calcIndex4Tag(Addr_t tag) const {
    Addr_t set;
    if(xorIndex) {
      tag = tag ^ (tag >> log2Sets);
      // Addr_t odd = (tag&1) | ((tag>>2) & 1) | ((tag>>4)&1) | ((tag>>6)&1) | ((tag>>8)&1) | ((tag>>10)&1) | ((tag>>12)&1) |
      // ((tag>>14)&1) | ((tag>>16)&1) | ((tag>>18)&1) | ((tag>>20)&1); // over 20 bit index???
      set = tag & maskSets;
    } else {
      set = tag & maskSets;
    }
    Addr_t index = set << log2Assoc;
    return index;
  }

  Addr_t calcAddr4Tag(Addr_t tag) const {
    return (tag << log2AddrLs);
  }
};

template <class State, class Addr_t> class CacheAssoc : public CacheGeneric<State, Addr_t> {
  using CacheGeneric<State, Addr_t>::numLines;
  using CacheGeneric<State, Addr_t>::assoc;
  using CacheGeneric<State, Addr_t>::maskAssoc;
  using CacheGeneric<State, Addr_t>::goodInterface;

private:
public:
  typedef typename CacheGeneric<State, Addr_t>::CacheLine Line;

protected:
  Line *            mem;
  Line **           content;
  uint16_t          irand;
  ReplacementPolicy policy;

  friend class CacheGeneric<State, Addr_t>;
  CacheAssoc(int32_t size, int32_t assoc, int32_t blksize, int32_t addrUnit, const char *pStr, bool xr);

  Line *findLinePrivate(Addr_t addr, bool updateSHIP = false, Addr_t SHIP_signature = 0);

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

  Line *findLine2Replace(Addr_t addr, bool updateSHIP = false, Addr_t SHIP_signature = 0);
};

template <class State, class Addr_t> class CacheDM : public CacheGeneric<State, Addr_t> {
  using CacheGeneric<State, Addr_t>::numLines;
  using CacheGeneric<State, Addr_t>::goodInterface;

private:
public:
  typedef typename CacheGeneric<State, Addr_t>::CacheLine Line;

protected:
  Line * mem;
  Line **content;

  friend class CacheGeneric<State, Addr_t>;
  CacheDM(int32_t size, int32_t blksize, int32_t addrUnit, const char *pStr, bool xr);

  Line *findLinePrivate(Addr_t addr, bool updateSHIP = false, Addr_t SHIP_signature = 0);

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

  Line *findLine2Replace(Addr_t addr, bool updateSHIP = false, Addr_t SHIP_signature = 0);
};

template <class State, class Addr_t> class CacheDMSkew : public CacheGeneric<State, Addr_t> {
  using CacheGeneric<State, Addr_t>::numLines;
  using CacheGeneric<State, Addr_t>::goodInterface;

private:
public:
  typedef typename CacheGeneric<State, Addr_t>::CacheLine Line;

protected:
  Line * mem;
  Line **content;

  friend class CacheGeneric<State, Addr_t>;
  CacheDMSkew(int32_t size, int32_t blksize, int32_t addrUnit, const char *pStr);

  Line *findLinePrivate(Addr_t addr, bool updateSHIP = false, Addr_t SHIP_signature = 0);

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

  Line *findLine2Replace(Addr_t addr, bool updateSHIP = false, Addr_t SHIP_signature = 0);
};

template <class State, class Addr_t> class CacheSHIP : public CacheGeneric<State, Addr_t> {
  using CacheGeneric<State, Addr_t>::numLines;
  using CacheGeneric<State, Addr_t>::assoc;
  using CacheGeneric<State, Addr_t>::maskAssoc;
  using CacheGeneric<State, Addr_t>::goodInterface;

private:
public:
  typedef typename CacheGeneric<State, Addr_t>::CacheLine Line;

protected:
  Line *            mem;
  Line **           content;
  uint16_t          irand;
  ReplacementPolicy policy;

  /***** SHIP ******/
  uint8_t *SHCT; // (2^log2shct) entries
  uint32_t log2shct;
  /*****************/

  friend class CacheGeneric<State, Addr_t>;
  CacheSHIP(int32_t size, int32_t assoc, int32_t blksize, int32_t addrUnit, const char *pStr,
            uint32_t shct_size = 13); // 13 was the optimal size in the paper

  Line *findLinePrivate(Addr_t addr, bool updateSHIP = false, Addr_t SHIP_signature = 0);

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

  Line *findLine2Replace(Addr_t addr, bool updateSHIP = false, Addr_t SHIP_signature = 0);
};

template <class Addr_t> class StateGenericShip {
private:
  Addr_t tag;
  /* SHIP */
  uint8_t rrpv;      // One per cache line
  Addr_t  signature; // One per cache line
  bool    outcome;   // One per cache line
  /* **** */

public:
  virtual ~StateGenericShip() {
    tag = 0;
  }

  Addr_t getTag() const {
    return tag;
  }
  void setTag(Addr_t a) {
    I(a);
    tag = a;
  }
  void clearTag() {
    tag = 0;
    initSHIP();
  }
  void initialize(void *c) {
    (void)c;
    clearTag();
  }

  void initSHIP() {
    rrpv      = RRIP_M - 1;
    signature = 0;
    outcome   = false;
  }

  Addr_t getSignature() const {
    return signature;
  }
  void setSignature(Addr_t a) {
    signature = a;
  }
  bool getOutcome() const {
    return outcome;
  }
  void setOutcome(bool a) {
    outcome = a;
  }
  uint8_t getRRPV() const {
    return rrpv;
  }

  void setRRPV(uint8_t a) {
    rrpv = a;
    if(rrpv > (RRIP_M - 1))
      rrpv = RRIP_M - 1;
    if(rrpv < 0)
      rrpv = 0;
  }

  void incRRPV() {
    if(rrpv < (RRIP_M - 1))
      rrpv++;
  }

  virtual bool isValid() const {
    return tag;
  }

  virtual void invalidate() {
    clearTag();
  }

  virtual void dump(const char *str) {
    (void)str;
  }
};

template <class Addr_t> class StateGeneric {
private:
  Addr_t tag;

public:
  virtual ~StateGeneric() {
    tag = 0;
  }

  Addr_t getTag() const {
    return tag;
  }
  void setTag(Addr_t a) {
    I(a);
    tag = a;
  }
  void clearTag() {
    tag = 0;
  }
  void initialize(void *c) {
    (void)c;
    clearTag();
  }

  virtual bool isValid() const {
    return tag;
  }

  virtual void invalidate() {
    clearTag();
  }

  virtual void dump(const char *str) {
    (void)str;
  }

  Addr_t getSignature() const {
    return 0;
  }
  void setSignature(Addr_t a) {
    (void)a;
    I(0); // Incorrect state used for SHIP
  }
  bool getOutcome() const {
    return 0;
  }
  void setOutcome(bool a) {
    (void)a;
    I(0); // Incorrect state used for SHIP
  }
  uint8_t getRRPV() const {
    return 0;
  }

  void setRRPV(uint8_t a) {
    (void)a;
    I(0); // Incorrect state used for SHIP
  }

  void incRRPV() {
    I(0); // Incorrect state used for SHIP
  }
};


#include <stdarg.h>
#include <string.h>
#include <strings.h>

#define k_RANDOM "RANDOM"
#define k_LRU "LRU"
#define k_LRUp "LRUp"
#define k_SHIP "SHIP"

//
// Class CacheGeneric, the combinational logic of Cache
//
template <class State, class Addr_t>
CacheGeneric<State, Addr_t> *CacheGeneric<State, Addr_t>::create(int32_t size, int32_t assoc, int32_t bsize, int32_t addrUnit,
                                                                 const char *pStr, bool skew, bool xr, uint32_t shct_size) {
  CacheGeneric *cache;

  if(skew) {
    I(assoc == 1); // Skew cache should be direct map
    cache = new CacheDMSkew<State, Addr_t>(size, bsize, addrUnit, pStr);
  } else if(assoc == 1) {
    // Direct Map cache
    cache = new CacheDM<State, Addr_t>(size, bsize, addrUnit, pStr, xr);
  } else if(size == (assoc * bsize)) {
    if(strcasecmp(pStr, k_SHIP) != 0) {
      // TODO: Fully assoc can use STL container for speed
      cache = new CacheAssoc<State, Addr_t>(size, assoc, bsize, addrUnit, pStr, xr);
    } else {
      // SHIP Cache
      cache = new CacheSHIP<State, Addr_t>(size, assoc, bsize, addrUnit, pStr, shct_size);
    }
  } else {
    if(strcasecmp(pStr, k_SHIP) != 0) {
      // Associative Cache
      cache = new CacheAssoc<State, Addr_t>(size, assoc, bsize, addrUnit, pStr, xr);
    } else {
      // SHIP Cache
      cache = new CacheSHIP<State, Addr_t>(size, assoc, bsize, addrUnit, pStr, shct_size);
    }
  }

  I(cache);
  return cache;
}

template <class State, class Addr_t> void CacheGeneric<State, Addr_t>::createStats(const char *section, const char *name) {
  (void)section;
  (void)name;
#if 0
  int32_t procId = 0;
  if ( name[0] == 'P' && name[1] == '(' ) {
    // This structure is assigned to an specific processor
    const char *number = &name[2];
    procId = atoi(number);
  }
#endif
}

template <class State, class Addr_t>
CacheGeneric<State, Addr_t> *CacheGeneric<State, Addr_t>::create(const char *section, const char *append, const char *format, ...) {
  CacheGeneric *cache = 0;
  char          size[STR_BUF_SIZE];
  char          bsize[STR_BUF_SIZE];
  char          addrUnit[STR_BUF_SIZE];
  char          assoc[STR_BUF_SIZE];
  char          repl[STR_BUF_SIZE];
  char          skew[STR_BUF_SIZE];

  snprintf(size, STR_BUF_SIZE, "%sSize", append);
  snprintf(bsize, STR_BUF_SIZE, "%sBsize", append);
  snprintf(addrUnit, STR_BUF_SIZE, "%sAddrUnit", append);
  snprintf(assoc, STR_BUF_SIZE, "%sAssoc", append);
  snprintf(repl, STR_BUF_SIZE, "%sReplPolicy", append);
  snprintf(skew, STR_BUF_SIZE, "%sSkew", append);

  int32_t     s    = 16 * 1024 * 1024;
  int32_t     a    = 32;
  int32_t     b    = 64;
  bool        xr   = false;
  bool        sk   = false;
  int32_t     u    = 1;
  const char *pStr = "LRU";

  // SHIP
  uint32_t shct_size = 0;
  if(strcasecmp(pStr, k_SHIP) == 0) {
    shct_size = 0;
  }

  cache = create(s, a, b, u, pStr, sk, xr, shct_size);

  I(cache);

  char cacheName[STR_BUF_SIZE];
  {
    va_list ap;

    va_start(ap, format);
    vsprintf(cacheName, format, ap);
    va_end(ap);
  }
  cache->createStats(section, cacheName);

  return cache;
}

/*********************************************************
 *  CacheAssoc
 *********************************************************/

template <class State, class Addr_t>
CacheAssoc<State, Addr_t>::CacheAssoc(int32_t size_, int32_t assoc_, int32_t blksize, int32_t addrUnit_, const char *pStr, bool xr)
    : CacheGeneric<State, Addr_t>(size_, assoc_, blksize, addrUnit_, xr) {
  I(numLines > 0);

  if(strcasecmp(pStr, k_RANDOM) == 0)
    policy = RANDOM;
  else if(strcasecmp(pStr, k_LRU) == 0)
    policy = LRU;
  else if(strcasecmp(pStr, k_LRUp) == 0)
    policy = LRUp;
  else {
    fmt::print("ERROR: Invalid cache policy [{}]\n", pStr);
    exit(0);
  }

  mem = (Line *)malloc(sizeof(Line) * (numLines + 1));
  ////read
  for(uint32_t i = 0; i < numLines; i++) {
    new(&mem[i]) Line(blksize);
  }
  content = new Line *[numLines + 1];

  for(uint32_t i = 0; i < numLines; i++) {
    mem[i].initialize(this);
    mem[i].invalidate();
    content[i] = &mem[i];
  }

  irand = 0;
}

template <class State, class Addr_t>
typename CacheAssoc<State, Addr_t>::Line *CacheAssoc<State, Addr_t>::findLinePrivate(Addr_t addr, bool updateSHIP,
                                                                                     Addr_t SHIP_signature) {
  (void)updateSHIP;
  (void)SHIP_signature;
  Addr_t tag = this->calcTag(addr);

  Line **theSet = &content[this->calcIndex4Tag(tag)];

  // Check most typical case
  if((*theSet)->getTag() == tag) {
    // I((*theSet)->isValid());   // TODO: why is this assertion failing
    return *theSet;
  }

  Line **lineHit = 0;
  Line **setEnd  = theSet + assoc;

  // For sure that position 0 is not (short-cut)
  {
    Line **l = theSet + 1;
    while(l < setEnd) {
      if((*l)->getTag() == tag) {
        lineHit = l;
        break;
      }
      l++;
    }
  }

  if(lineHit == 0)
    return 0;

  // I((*lineHit)->isValid()); //TODO: see why this assertion is failing

  // No matter what is the policy, move lineHit to the *theSet. This
  // increases locality
  Line *tmp = *lineHit;
  {
    Line **l = lineHit;
    while(l > theSet) {
      Line **prev = l - 1;
      *l          = *prev;
      ;
      l = prev;
    }
    *theSet = tmp;
  }

  return tmp;
}

template <class State, class Addr_t>
typename CacheAssoc<State, Addr_t>::Line *CacheAssoc<State, Addr_t>::findLine2Replace(Addr_t addr, bool updateSHIP,
                                                                                      Addr_t SHIP_signature) {
  (void)updateSHIP;
  (void)SHIP_signature;
  Addr_t tag = this->calcTag(addr);
  I(tag);
  Line **theSet = &content[this->calcIndex4Tag(tag)];

  // Check most typical case
  if((*theSet)->getTag() == tag) {
    // GI(tag,(*theSet)->isValid()); //TODO: why is this assertion failing
    return *theSet;
  }

  Line **lineHit  = 0;
  Line **lineFree = 0; // Order of preference, invalid
  Line **setEnd   = theSet + assoc;

  {
    Line **l = setEnd - 1;
    while(l >= theSet) {
      if((*l)->getTag() == tag) {
        lineHit = l;
        break;
      }
      if(!(*l)->isValid())
        lineFree = l;
      else if(lineFree == 0)
        lineFree = l;

      l--;
    }
  }

  Line * tmp;
  Line **tmp_pos;
  if(!lineHit) {

    I(lineFree);

    if(policy == RANDOM) {
      lineFree = &theSet[irand];
      irand    = (irand + 1) & maskAssoc;
      if(irand == 0)
        irand = (irand + 1) & maskAssoc; // Not MRU
    } else {
      I(policy == LRU || policy == LRUp);
      // Get the oldest line possible
      lineFree = setEnd - 1;
    }

    I(lineFree);

    if(lineFree == theSet)
      return *lineFree; // Hit in the first possition

    tmp     = *lineFree;
    tmp_pos = lineFree;
  } else {
    tmp     = *lineHit;
    tmp_pos = lineHit;
  }

  if(policy == LRUp) {
#if 0
    Line **l = tmp_pos;
    while(l > &theSet[13]) {
      Line **prev = l - 1;
      *l = *prev;;
      l = prev;
    }
    theSet[13] = tmp;
#endif
  } else {
    Line **l = tmp_pos;
    while(l > theSet) {
      Line **prev = l - 1;
      *l          = *prev;
      ;
      l = prev;
    }
    *theSet = tmp;
  }

  return tmp;
}

/*********************************************************
 *  CacheDM
 *********************************************************/

template <class State, class Addr_t>
CacheDM<State, Addr_t>::CacheDM(int32_t size_, int32_t blksize, int32_t addrUnit_, const char *pStr, bool xr)
    : CacheGeneric<State, Addr_t>(size_, 1, blksize, addrUnit_, xr) {
  (void)pStr;
  I(numLines > 0);

  mem = (Line *)malloc(sizeof(Line) * (numLines + 1));
  for(uint32_t i = 0; i < numLines; i++) {
    new(&mem[i]) Line(blksize);
  }
  content = new Line *[numLines + 1];

  for(uint32_t i = 0; i < numLines; i++) {
    mem[i].initialize(this);
    mem[i].invalidate();
    content[i] = &mem[i];
  }
}

template <class State, class Addr_t>
typename CacheDM<State, Addr_t>::Line *CacheDM<State, Addr_t>::findLinePrivate(Addr_t addr, bool updateSHIP,
                                                                               Addr_t SHIP_signature) {
  (void)updateSHIP;
  (void)SHIP_signature;
  Addr_t tag = this->calcTag(addr);
  I(tag);

  Line *line = content[this->calcIndex4Tag(tag)];

  if(line->getTag() == tag) {
    I(line->isValid());
    return line;
  }

  return 0;
}

template <class State, class Addr_t>
typename CacheDM<State, Addr_t>::Line *CacheDM<State, Addr_t>::findLine2Replace(Addr_t addr, bool updateSHIP,
                                                                                Addr_t SHIP_signature) {
  (void)updateSHIP;
  (void)SHIP_signature;
  Addr_t tag  = this->calcTag(addr);
  Line * line = content[this->calcIndex4Tag(tag)];

  return line;
}

/*********************************************************
 *  CacheDMSkew
 *********************************************************/

template <class State, class Addr_t>
CacheDMSkew<State, Addr_t>::CacheDMSkew(int32_t size_, int32_t blksize, int32_t addrUnit_, const char *pStr)
    : CacheGeneric<State, Addr_t>(size_, 1, blksize, addrUnit_, false) {
  (void)pStr;
  I(numLines > 0);

  mem = (Line *)malloc(sizeof(Line) * (numLines + 1));
  for(uint32_t i = 0; i < numLines; i++) {
    new(&mem[i]) Line(blksize);
  }
  content = new Line *[numLines + 1];

  for(uint32_t i = 0; i < numLines; i++) {
    mem[i].initialize(this);
    mem[i].invalidate();
    content[i] = &mem[i];
  }
}

template <class State, class Addr_t>
typename CacheDMSkew<State, Addr_t>::Line *CacheDMSkew<State, Addr_t>::findLinePrivate(Addr_t addr, bool updateSHIP,
                                                                                       Addr_t SHIP_signature) {
  (void)updateSHIP;
  (void)SHIP_signature;
  Addr_t tag1 = this->calcTag(addr);
  I(tag1);

  Line *line = content[this->calcIndex4Tag(tag1)];

  if(line->getTag() == tag1) {
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

  if(line->getTag() == tag1) { // FIRST TAG, tag2 is JUST used for indexing the table
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

  if(line->getTag() == tag1) { // FIRST TAG, tag2 is JUST used for indexing the table
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
typename CacheDMSkew<State, Addr_t>::Line *CacheDMSkew<State, Addr_t>::findLine2Replace(Addr_t addr, bool updateSHIP,
                                                                                        Addr_t SHIP_signature) {
  (void)updateSHIP;
  (void)SHIP_signature;
  Addr_t tag1  = this->calcTag(addr);
  Line * line1 = content[this->calcIndex4Tag(tag1)];

  if(line1->getTag() == tag1) {
    GI(tag1, line1->isValid());
    return line1;
  }

  // BEGIN Skew cache
  // Addr_t tag2 = (tag1 ^ (tag1>>1));
  Addr_t addrh = (addr >> 5) ^ (addr >> 11);
  Addr_t tag2  = this->calcTag(addrh);
  Line * line2 = content[this->calcIndex4Tag(tag2)];

  if(line2->getTag() == tag1) { // FIRST TAG, tag2 is JUST used for indexing the table
    I(line2->isValid());
    return line2;
  }

  static int32_t rand_number = 0;
  rand_number++;

#if 1
  // Addr_t tag3 = (tag1 ^ ((tag1>>1) + ((tag1 & 0xFFFF))));
  addrh        = addrh + (addr & 0xFF);
  Addr_t tag3  = this->calcTag(addrh);
  Line * line3 = content[this->calcIndex4Tag(tag3)];

  if(line3->getTag() == tag1) { // FIRST TAG, tag2 is JUST used for indexing the table
    I(line3->isValid());
    return line3;
  }

  while(1) {
    if(rand_number > 2)
      rand_number = 0;

    if(rand_number == 0) {
      if(line1->recent)
        line1->recent = false;
      else {
        line1->recent = true;
        line2->recent = false;
        line3->recent = false;
        return line1;
      }
    }
    if(rand_number == 1) {
      if(line2->recent)
        line2->recent = false;
      else {
        line1->recent = false;
        line2->recent = true;
        line3->recent = false;
        return line2;
      }
    } else {
      if(line3->recent)
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
  if((rand_number & 1) == 0)
    return line1;
  return line2;
#endif
  // END Skew cache
}

/*********************************************************
 *  CacheSHIP
 *********************************************************/

template <class State, class Addr_t>
CacheSHIP<State, Addr_t>::CacheSHIP(int32_t size_, int32_t assoc_, int32_t blksize, int32_t addrUnit_, const char *pStr,
                                    uint32_t shct_size)
    : CacheGeneric<State, Addr_t>(size_, assoc_, blksize, addrUnit_, false) {
  I(numLines > 0);
  log2shct = shct_size;
  I(shct_size < 31);
  SHCT = new uint8_t[(2 << log2shct)];

  if(strcasecmp(pStr, k_SHIP) != 0) {
    fmt::print("Invalid cache policy [{}]\n", pStr);
    exit(0);
  } else {
    policy = SHIP;
  }

  mem = (Line *)malloc(sizeof(Line) * (numLines + 1));
  for(uint32_t i = 0; i < numLines; i++) {
    new(&mem[i]) Line(blksize);
  }
  content = new Line *[numLines + 1];

  for(uint32_t i = 0; i < numLines; i++) {
    mem[i].initialize(this);
    mem[i].invalidate();
    content[i] = &mem[i];
  }

  for(uint32_t j = 0; j < (2 ^ log2shct); j++) {
    SHCT[j] = 0;
  }

  irand = 0;
}

template <class State, class Addr_t>
typename CacheSHIP<State, Addr_t>::Line *CacheSHIP<State, Addr_t>::findLinePrivate(Addr_t addr, bool updateSHIP,
                                                                                   Addr_t SHIP_signature) {
  Addr_t tag     = this->calcTag(addr);
  Line **theSet  = &content[this->calcIndex4Tag(tag)];
  Line **setEnd  = theSet + assoc;
  Line **lineHit = 0;

  {
    Line **l = theSet;
    while(l < setEnd) {
      if((*l)->getTag() == tag) {
        lineHit = l;
        break;
      }
      l++;
    }
  }

  if(lineHit == 0) {
    // Cache MISS
    return 0;
  }

  I((*lineHit)->isValid());
  // Cache HIT
  if(updateSHIP) {
    // SHIP_signature is the PC or the memory address, not the usable signature
    Addr_t signature = (SHIP_signature ^ ((SHIP_signature >> 1) + ((SHIP_signature & 0xFFFFFFFF))));
    signature &= ((2 << log2shct) - 1);

    (*lineHit)->setOutcome(true);

    if(SHCT[signature] < 7) // 3 bit counter. But why? Whatabout 2^log2Assoc - 1
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
typename CacheSHIP<State, Addr_t>::Line *CacheSHIP<State, Addr_t>::findLine2Replace(Addr_t addr, bool updateSHIP,
                                                                                    Addr_t SHIP_signature) {

  Addr_t tag = this->calcTag(addr);
  I(tag);

  Line **theSet = &content[this->calcIndex4Tag(tag)];
  Line **setEnd = theSet + assoc;

  Line **lineFree = 0; // Order of preference, invalid, rrpv = 3
  Line **lineHit  = 0; // Exact tag match

  {
    Line **l = theSet;
    do {
      l = theSet;

      while(l < setEnd) {
        // Tag matches
        if((*l)->getTag() == tag) {
          lineHit = l;
          break;
        }

        // Line is invalid
        if(!(*l)->isValid()) {
          lineFree = l;
          break;
        }

        // Line is distant re-referenced
        if((*l)->getRRPV() >= RRIP_M - 1) {
          lineFree = l;
          break;
        }

        l++;
      }

      if((lineHit == 0) && (lineFree == 0)) {
        l = theSet;
        while(l < setEnd) {
          (*l)->incRRPV();
          l++;
        }
      }
    } while((lineHit == 0) && (lineFree == 0));
  }

  Line *tmp;
  if(!lineHit) {
    tmp = *lineFree;
  } else {
    tmp = *lineHit;
  }

  // updateSHIP = false;
  if(updateSHIP) {
    // SHIP_signature is the PC or the memory address, not the usable signature
    Addr_t signature = (SHIP_signature ^ ((SHIP_signature >> 1) + ((SHIP_signature & 0xFFFFFFFF))));
    signature &= ((2 << log2shct) - 1);

    if((tmp->isValid())) {
      Addr_t signature_m = (tmp)->getSignature();
      if((tmp)->getOutcome() == false) {
        if(SHCT[signature_m] > 0) {
          SHCT[signature_m] -= 1;
        }
      }
    }

    (tmp)->setOutcome(false);
    (tmp)->setSignature(signature);
    if(SHCT[signature] == 0) {
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

