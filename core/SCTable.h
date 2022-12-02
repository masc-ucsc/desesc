// See LICENSE for details

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "iassert.hpp"
#include "snippets.hpp"

class SCTable {
private:
  const uint64_t sizeMask;
  const uint8_t  Saturate;
  const uint8_t  MaxValue;

  uint8_t *table;

protected:
public:
  SCTable(const std::string &str, size_t size, uint8_t bits = 2);
  ~SCTable(void);
  void clear(uint32_t cid);  // Bias to not-taken
  void reset(uint32_t cid, bool taken);
  bool predict(uint32_t cid, bool taken);  // predict and update
  void update(uint32_t cid, bool taken);
  void inc(uint32_t cid, int d);
  void dec(uint32_t cid, int d);

  bool    predict(uint32_t cid) const { return table[cid & sizeMask] >= Saturate; }
  bool    isLowest(uint32_t cid) const { return table[cid & sizeMask] == 0; }
  bool    isHighest(uint32_t cid) const { return table[cid & sizeMask] == MaxValue; }
  uint8_t getValue(uint32_t cid) const { return table[cid & sizeMask]; }
};
