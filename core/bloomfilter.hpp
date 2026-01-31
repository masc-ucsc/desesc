// See LICENSE for details.

#pragma once

#include <array>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "iassert.hpp"
#include "stats.hpp"

class BloomFilter {
private:
  std::vector<int32_t>              vSize;
  std::vector<int32_t>              vBits;
  std::vector<unsigned>             vMask;
  std::vector<int32_t>              rShift;
  std::vector<std::vector<int32_t>> countVec;
  int32_t                           nVectors;
  std::vector<int32_t>              nonZeroCount;
  const char*                       desc{nullptr};
  int32_t                           nElements;

  bool BFBuild;

  void    initMasks();
  int32_t getIndex(unsigned val, int32_t chunkPos);

public:
  ~BloomFilter() = default;

  // the chunk parameters are from the least significant to
  // the most significant portion of the address
  BloomFilter(const std::vector<int>& bits, const std::vector<int>& size);
  BloomFilter() : BFBuild(false) {}

  BloomFilter(const BloomFilter& bf);

  BloomFilter& operator=(const BloomFilter& bf);

  void insert(unsigned e);
  void remove(unsigned e);

  void clear();

  [[nodiscard]] bool mayExist(unsigned e);
  [[nodiscard]] bool mayIntersect(BloomFilter& otherbf);

  void intersectionWith(BloomFilter& otherbf, BloomFilter& inter);

  void mergeWith(BloomFilter& otherbf);
  void subtract(BloomFilter& otherbf);

  [[nodiscard]] bool isSubsetOf(BloomFilter& otherbf);

  [[nodiscard]] int32_t countAlias(unsigned e);

  void                      dump(const char* msg);
  [[nodiscard]] const char* getDesc() const noexcept { return desc; }

  [[nodiscard]] int32_t size() const noexcept {  // # of elements encoded
    return nElements;
  }

  [[nodiscard]] int32_t getSize() const;  // size of the vectors in bits
  [[nodiscard]] int32_t getSizeRLE(int32_t base = 0, int32_t runBits = 7) const;

  FILE*          dumpPtr;
  static int32_t numDumps;
  void           begin_dump_pychart(const char* bname = "bf");
  void           end_dump_pychart();
  void           add_dump_line(unsigned e);
};

class BitSelection {
private:
  int32_t                  nBits;
  std::array<int32_t, 32>  bits;
  std::array<unsigned, 32> mask;

public:
  BitSelection() : nBits{0}, bits{}, mask{} {}

  BitSelection(int32_t* bitPos, int32_t n) {
    nBits = 0;
    for (int32_t i = 0; i < n; i++) {
      addBit(bitPos[i]);
    }
  }

  ~BitSelection() = default;

  [[nodiscard]] int32_t getNBits() const noexcept { return nBits; }

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

  void dump(const char* msg) {
    printf("%s:", msg);
    for (int32_t i = 0; i < nBits; i++) {
      printf(" %d", bits[i]);
    }
    printf("\n");
  }
};
