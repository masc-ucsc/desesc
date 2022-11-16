// See LICENSE for details.

#pragma once

#include <cstdint>
#include <vector>

#include "stats.hpp"
#include "iassert.hpp"

class BloomFilter {
private:
  int32_t  *vSize;
  int32_t  *vBits;
  unsigned *vMask;
  int32_t  *rShift;
  int32_t **countVec;
  int32_t   nVectors;
  int32_t  *nonZeroCount;
  char     *desc;
  int32_t   nElements;

  bool BFBuild;

  void    initMasks();
  int32_t getIndex(unsigned val, int32_t chunkPos);

public:
  ~BloomFilter();

  // the chunk parameters are from the least significant to
  // the most significant portion of the address
  BloomFilter(const std::vector<int> &bits, const std::vector<int> &size);
  BloomFilter() : BFBuild(false) {}

  BloomFilter(const BloomFilter &bf);

  BloomFilter &operator=(const BloomFilter &bf);

  void insert(unsigned e);
  void remove(unsigned e);

  void clear();

  bool mayExist(unsigned e);
  bool mayIntersect(BloomFilter &otherbf);

  void intersectionWith(BloomFilter &otherbf, BloomFilter &inter);

  void mergeWith(BloomFilter &otherbf);
  void subtract(BloomFilter &otherbf);

  bool isSubsetOf(BloomFilter &otherbf);

  int32_t countAlias(unsigned e);

  void        dump(const char *msg);
  const char *getDesc() { return desc; }

  int32_t size() {  //# of elements encoded
    return nElements;
  }

  int32_t getSize();  // size of the vectors in bits
  int32_t getSizeRLE(int32_t base = 0, int32_t runBits = 7);

  FILE          *dumpPtr;
  static int32_t numDumps;
  void           begin_dump_pychart(const char *bname = "bf");
  void           end_dump_pychart();
  void           add_dump_line(unsigned e);
};

class BitSelection {
private:
  int32_t  nBits;
  int32_t  bits[32];
  unsigned mask[32];

public:
  BitSelection() {
    nBits = 0;
    for (int32_t i = 0; i < 32; i++) {
      bits[i] = 0;
    }
  }

  BitSelection(int32_t *bitPos, int32_t n) {
    nBits = 0;
    for (int32_t i = 0; i < n; i++) addBit(bitPos[i]);
  }

  ~BitSelection() {}

  int32_t getNBits() { return nBits; }

  void addBit(int32_t b) {
    bits[nBits] = b;
    mask[nBits] = 1 << b;
    nBits++;
  }

  unsigned permute(unsigned val) {
    unsigned res = 0;
    for (int32_t i = 0; i < nBits; i++) {
      unsigned bit = (val & mask[i]) ? 1 : 0;
      res          = res | (bit << i);
    }
    return res;
  }

  void dump(const char *msg) {
    printf("%s:", msg);
    for (int32_t i = 0; i < nBits; i++) {
      printf(" %d", bits[i]);
    }
    printf("\n");
  }
};

