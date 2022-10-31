// See LICENSE for details.

#include <typeinfo>

#include "iassert.hpp"
#include "snippets.hpp"

short log2i(uint32_t n) {
  uint32_t m = 1;
  uint32_t i = 0;

  if(n == 1)
    return 0;

  n = roundUpPower2(n);
  // assume integer power of 2
  I((n & (n - 1)) == 0);

  while(m < n) {
    i++;
    m <<= 1;
  }

  return i;
}

// this routine computes the smallest power of 2 greater than the
// parameter
uint32_t roundUpPower2(uint32_t x) {
  // efficient branchless code extracted from "Hacker's Delight" by
  // Henry S. Warren, Jr.

  x = x - 1;
  x = x | (x >> 1);
  x = x | (x >> 2);
  x = x | (x >> 4);
  x = x | (x >> 8);
  x = x | (x >> 16);
  return x + 1;
}
