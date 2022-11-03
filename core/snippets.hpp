// See LICENSE for details.

#pragma once

#include <cstdint>

//**************************************************
// Types used for time (move to callback?)
typedef uint64_t Time_t;
const uint64_t   MaxTime = ((~0ULL) - 1024);  // -1024 is to give a little bit of margin

extern Time_t globalClock;  // Defined in Thread.cpp
extern Time_t deadClock;    // Defined in Thread.cpp

typedef uint16_t TimeDelta_t;
const uint16_t   MaxDeltaTime = (65535 - 1024);  // -1024 is to give a little bit of margin

// x, y are integers and x,y > 0
#define CEILDiv(x, y) ((x)-1) / (y) + 1

uint32_t roundUpPower2(uint32_t x);
short    log2i(uint32_t n);

#define ISPOWER2(x) (((x) & (x - 1)) == 0)

#define AtomicSwap(ptr, val)                     __sync_lock_test_and_set(ptr, val)
#define AtomicCompareSwap(ptr, tst_ptr, new_ptr) __sync_val_compare_and_swap(ptr, tst_ptr, new_ptr)
#define AtomicAdd(ptr, val)                      __sync_fetch_and_add(ptr, val)
#define AtomicSub(ptr, val)                      __sync_fetch_and_sub(ptr, val)

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
