// Acknowledgement: This file uses cache_test.cpp as a template

// This tests the exclusive spectre-safe cache, whose implementation
// currently takes advantage of the victim configuration

// See LICENSE for details.

// cache test
//
// TODO:
//
// -no double snoop detect
//
// -Create represure in the L1 cache with requests.
//
// -3 cores doing a write simultaneously, how to handle snoops

#include <cstdio>
#include <exception>

#include "gtest/gtest.h"

#include "fmt/format.h"
#include "iassert.hpp"

#include "config.hpp"
#include "report.hpp"
#include "ccache.hpp"
#include "dinst.hpp"
#include "gprocessor.hpp"
#include "instruction.hpp"
#include "memobj.hpp"
#include "memrequest.hpp"
#include "memstruct.hpp"
#include "memory_system.hpp"
#include "callback.hpp"

using ::testing::EmptyTestEventListener;
using ::testing::InitGoogleTest;
using ::testing::Test;
using ::testing::TestCase;
using ::testing::TestEventListeners;
using ::testing::TestInfo;
using ::testing::TestPartResult;
using ::testing::UnitTest;

#ifdef DEBUG_CALLPATH
extern bool forcemsgdump;
#endif

static int rd_pending     = 0;
static int wr_pending     = 0;
static int num_operations = 0;

enum currState {
  Modified,   // 0
  Exclusive,  // 1
  Shared,     // 2
  Invalid     // 3
};

currState getState(CCache *cache, Addr_t addr) {
  currState state;

  if (cache->Modified(addr)) {
    state = Modified;
  }

  if (cache->Exclusive(addr)) {
    state = Exclusive;
  }

  if (cache->Shared(addr)) {
    state = Shared;
  }

  if (cache->Invalid(addr)) {
    state = Invalid;
  }

  return state;
}

void rdDone(Dinst *dinst) {
  //printf("rddone @%lld\n", (long long)globalClock);

  rd_pending--;
  dinst->scrap();
}

void wrDone(Dinst *dinst) {
  //printf("wrdone @%lld\n", (long long)globalClock);

  wr_pending--;
  dinst->scrap();
}

static void waitAllMemOpsDone() {
  while (rd_pending || wr_pending) {
    EventScheduler::advanceClock();
  }
}

typedef CallbackFunction1<Dinst *, &rdDone> rdDoneCB;
typedef CallbackFunction1<Dinst *, &wrDone> wrDoneCB;

// non-speculative read by default
static void doread(MemObj *cache, Addr_t addr, bool spec = false) {
  num_operations++;

  auto *ldClone = Dinst::create(
      Instruction(iLALU_LD, LREG_R1, LREG_R2, LREG_R3, LREG_R4)
      ,0xdeaddead // pc
      ,addr
      ,0
      ,true
    );

  while (cache->isBusy(addr)) {
    EventScheduler::advanceClock();
  }

  rdDoneCB *cb = rdDoneCB::create(ldClone);
  //printf("rd %x @%lld\n", (unsigned int)addr, (long long)globalClock);

  if (spec) {
    MemRequest::sendSpecReqDL1Read(cache, ldClone->has_stats(), ldClone->getAddr(), ldClone->getPC(), /*Dinst *dinst*/ 0, cb);
  } else {
    MemRequest::sendSafeReqDL1Read(cache, ldClone->has_stats(), ldClone->getAddr(), ldClone->getPC(), /*Dinst *dinst*/ 0, cb);
  }
  rd_pending++;
}

static void doprefetch(MemObj *cache, Addr_t addr) {
  num_operations++;

  auto *ldClone = Dinst::create(
      Instruction(iLALU_LD, LREG_R5, LREG_R6, LREG_R7, LREG_R8)
      ,0xbeefbeef // pc
      ,addr
      ,0
      ,true
    );

  while (cache->isBusy(addr)) {
    EventScheduler::advanceClock();
  }

  rdDoneCB *cb = rdDoneCB::create(ldClone);
  //printf("rd %x @%lld\n", (unsigned int)addr, (long long)globalClock);

  cache->tryPrefetch(ldClone->has_stats(), ldClone->getAddr(), 1, 0xF00D, ldClone->getPC(), cb);
  rd_pending++;
}

static void dowrite(MemObj *cache, Addr_t addr) {
  num_operations++;

  auto *stClone = Dinst::create(
      Instruction(iSALU_ST, LREG_R5, LREG_R6, LREG_R7, LREG_R8)
      ,0x200 // pc
      ,addr
      ,0
      ,true
    );

  while (cache->isBusy(addr)) {
    EventScheduler::advanceClock();
  }

  wrDoneCB *cb = wrDoneCB::create(stClone);
  //printf("wr %x @%lld\n", (unsigned int)addr, (long long)globalClock);

  MemRequest::sendReqWrite(cache, stClone->has_stats(), stClone->getAddr(), stClone->getPC(), cb);
  wr_pending++;
}

// return an address based on the cache level,
// the specified tag, index, and offeset
static Addr_t designAddr(Addr_t level, Addr_t tag, Addr_t index, Addr_t offset) {
  // hardcoded offset 6 bits
  Addr_t index_base = 1 << 6;
  Addr_t tag_base;
  if (level == 1) {
    // hardcoded L1 index bits
    // 32768 / 64 / 4 = 128 sets: index 7 bits
    tag_base = 1 << 13;
  } else if (level == 2) {
    // hardcoded L2 index bits
    // 1048576 / 64 / 4 = 4096 sets: index 12 bits
    tag_base = 1 << 18;
  } else {
    EXPECT_EQ(true, false);
  }
  return tag * tag_base + index * index_base + offset;
}

Gmemory_system *gms_p0    = nullptr;
Gmemory_system *gms_p1    = nullptr;

static void setup_config() {
  std::ofstream file;

  file.open("cachecore.toml");

  file <<
"[soc]\n"
"core = [\"c0\",\"c0\"]\n"
"[c0]\n"
"type  = \"ooo\"\n"
"caches        = true\n"
"dl1           = \"dl1_cache DL1\"\n"
"il1           = \"dl1_cache IL1\"\n"
"[dl1_cache]\n"
"type       = \"cache\"\n"
"cold_misses = true\n"
"size       = 32768\n"
"line_size  = 64\n"
"delay      = 5\n"
"miss_delay = 2\n"
"assoc      = 4\n"
"repl_policy = \"lru\"\n"
"port_occ   = 1\n"
"port_num   = 1\n"
"port_banks = 32\n"
"send_port_occ = 1\n"
"send_port_num = 1\n"
"max_requests  = 32\n"
"allocate_miss = true\n"
"victim        = true   # exclusive spectre-safe\n"
"coherent      = true\n"
"inclusive     = false  # exclusive\n"
"directory     = false\n"
"nlp_distance = 2\n"
"nlp_degree   = 1       # 0 disabled\n"
"nlp_stride   = 1\n"
"drop_prefetch = true\n"
"prefetch_degree = 0    # 0 disabled\n"
"mega_lines1K    = 8    # 8 lines touched, triggers mega/carped prefetch\n"
"lower_level = \"privl2 L2 sharedby 2\"\n"
"[privl2]\n"
"type       = \"cache\"\n"
"cold_misses = true\n"
"size       = 1048576\n"
"line_size  = 64\n"
"delay      = 13\n"
"miss_delay = 7\n"
"assoc      = 4\n"
"repl_policy = \"lru\"\n"
"port_occ   = 1\n"
"port_num   = 1\n"
"port_banks = 32\n"
"send_port_occ = 1\n"
"send_port_num = 1\n"
"max_requests  = 32\n"
"allocate_miss = true\n"
"victim        = true   # exclusive spectre-safe\n"
"coherent      = true\n"
"inclusive     = false  # exclusive\n"
"directory     = false\n"
"nlp_distance = 2\n"
"nlp_degree   = 1       # 0 disabled\n"
"nlp_stride   = 1\n"
"drop_prefetch = true\n"
"prefetch_degree = 0    # 0 disabled\n"
"mega_lines1K    = 8    # 8 lines touched, triggers mega/carped prefetch\n"
"lower_level = \"l3 l3 shared\"\n"
"[l3]\n"
"type       = \"nice\"\n"
"line_size  = 64\n"
"delay      = 31\n"
"cold_misses = false\n"
"lower_level = \"\"\n"
    ;

  file.close();
}

void            initialize() {
  static bool pluggedin = false;
  if (!pluggedin) {
    setup_config();

    Report::init();
    Config::init("cachecore.toml");

    gms_p0 = new Memory_system(0);
    gms_p1 = new Memory_system(1);
    pluggedin = true;
#ifdef DEBUG_CALLPATH
    forcemsgdump = true;
#endif

    Config::exit_on_error();
    EventScheduler::advanceClock();
  }
}

CCache *getDL1(Gmemory_system *gms) {
  MemObj *dl1 = gms->getDL1();
  if (dl1->get_type() != "cache") {
    fmt::print("ERROR: The first level must be a cache {}\n", dl1->get_type());
    exit(-1);
  }

  CCache *cdl1 = static_cast<CCache *>(dl1);
  return cdl1;
}

CCache *getL3(MemObj *L2) {
  MRouter *router2 = L2->getRouter();
  MemObj  *L3      = router2->getDownNode();
  if (L3->get_type() != "cache") {
    return nullptr;
  }

  CCache *l3c = static_cast<CCache *>(L3);
  return l3c;
}

CCache *getL2(MemObj *P0DL1) {
  MRouter *router = P0DL1->getRouter();
  MemObj  *L2     = router->getDownNode();
  I(L2->get_type() == "cache");

  CCache  *l2c    = static_cast<CCache *>(L2);
  // l2c->setNeedsCoherence();
  return l2c;
}

class CacheTest : public testing::Test {
protected:
  CCache      *p0dl1;
  CCache      *p0l2;
  CCache      *p1l2;
  CCache      *l3;
  CCache      *p1dl1;
  virtual void SetUp() {
    initialize();

    p0dl1 = getDL1(gms_p0);

    p0l2 = getL2(p0dl1);
    p0l2->setCoreDL1(0);  // to allow fake L2 direct accesses

    p1dl1 = getDL1(gms_p1);
    p1l2  = getL2(p1dl1);

    l3 = getL3(p0l2);
  }
  virtual void TearDown() {}
};

// Test exclusiveness

// read to L1, it should not be in L2
TEST_F(CacheTest, readL1_exclusive) {
  // addr for L1 index 1
  Addr_t addr = designAddr(1, 0, 1, 0);
  doread(p0dl1, addr);
  waitAllMemOpsDone();

  // ensure that the line is in L1 but not L2
  EXPECT_EQ(Exclusive, getState(p0dl1, addr));
  EXPECT_EQ(Invalid, getState(p0l2, addr));
}

// read to L2, then read to L1,
// it should no longer be in L2
TEST_F(CacheTest, readL2_readL1_invalidation) {
  // addr for L2 index 2
  Addr_t addr = designAddr(2, 0, 2, 0);
  doread(p0l2, addr);
  waitAllMemOpsDone();

  doread(p0dl1, addr);
  waitAllMemOpsDone();

  // ensure that the line is sent to L1
  EXPECT_EQ(Exclusive, getState(p0dl1, addr));
  EXPECT_EQ(Invalid, getState(p0l2, addr));
}

// read to L1, then displace it,
// it should be in L2
TEST_F(CacheTest, readL1_dispL1_victim) {
  // addr for L1 index 3 with different tag
  Addr_t to_evict = designAddr(1, 0, 3, 0);
  doread(p0dl1, to_evict);
  waitAllMemOpsDone();

  doread(p0dl1, designAddr(1, 1, 3, 0));
  doread(p0dl1, designAddr(1, 2, 3, 0));
  doread(p0dl1, designAddr(1, 3, 3, 0));
  waitAllMemOpsDone();

  // before eviction the line in in L1 but not L2
  EXPECT_EQ(Exclusive, getState(p0dl1, to_evict));
  EXPECT_EQ(Invalid, getState(p0l2, to_evict));

  // evict
  doread(p0dl1, designAddr(1, 4, 3, 0));
  waitAllMemOpsDone();

  // ensure that the evicted line is in L2
  EXPECT_EQ(Invalid, getState(p0dl1, to_evict));
  EXPECT_EQ(Exclusive, getState(p0l2, to_evict));
}

// read to L1, read to L2, then displace L2,
// it should still be in L1 since it does not send back invalidations
TEST_F(CacheTest, readL1L2_dispL2_no_bak_inv) {
  // addr for L2 index 4 with different tag
  Addr_t to_evict = designAddr(2, 0, 4, 0);

  // read to L1
  doread(p0dl1, to_evict);
  waitAllMemOpsDone();

  // read to L2
  doread(p0l2, to_evict);
  // there seem to be some strange re-orderings
  // have to finish this first
  waitAllMemOpsDone();

  doread(p0l2, designAddr(2, 1, 4, 0));
  doread(p0l2, designAddr(2, 2, 4, 0));
  doread(p0l2, designAddr(2, 3, 4, 0));
  waitAllMemOpsDone();

  // before eviction the line is in L1 and L2
  EXPECT_EQ(Exclusive, getState(p0dl1, to_evict));
  EXPECT_EQ(Exclusive, getState(p0l2, to_evict));

  // evict
  doread(p0l2, designAddr(2, 4, 4, 0));
  waitAllMemOpsDone();

  // ensure that the line is on longer in L2 but stays in L1
  EXPECT_EQ(Exclusive, getState(p0dl1, to_evict));
  EXPECT_EQ(Invalid, getState(p0l2, to_evict));
}

// Test spectre-safeness

// read to L1 speculatively,
// but it should not be a valid line because no speculative allocation is allowed
TEST_F(CacheTest, sreadL1_no_alloc) {
  // addr for L1 index 5
  Addr_t addr = designAddr(1, 0, 5, 0);

  // speculative read to L1
  bool spec = true;
  doread(p0dl1, addr, spec);
  waitAllMemOpsDone();

  // no allocation
  EXPECT_EQ(Invalid, getState(p0dl1, addr));
  EXPECT_EQ(Invalid, getState(p0l2, addr));
}

// read to L2, then read to L1 speculatively, 
// L2 should still have the line because no speculative invalidation is allowed
TEST_F(CacheTest, readL2_sreadL1_no_inv) {
  // addr for L2 index 6
  Addr_t addr = designAddr(2, 0, 6, 0);
  doread(p0l2, addr);
  waitAllMemOpsDone();

  // speculative read to L1
  bool spec = true;
  doread(p0dl1, addr, spec);
  waitAllMemOpsDone();

  // no allocation in L1
  // no invalidation in L2
  EXPECT_EQ(Invalid, getState(p0dl1, addr));
  EXPECT_EQ(Exclusive, getState(p0l2, addr));
}


// read to L1, and the following speculative read shall not update LRU,
// due to no effect look-up
TEST_F(CacheTest, readL1_sreadL1_no_LRU) {
  // addr for L1 index 7 with different tag
  Addr_t to_evict = designAddr(1, 0, 7, 0);
  doread(p0dl1, to_evict);
  waitAllMemOpsDone();
  doread(p0dl1, designAddr(1, 1, 7, 0));
  doread(p0dl1, designAddr(1, 2, 7, 0));
  doread(p0dl1, designAddr(1, 3, 7, 0));
  waitAllMemOpsDone();

  // speculative read to L1
  // it shall not update LRU
  bool spec = true;
  doread(p0dl1, to_evict, spec);
  waitAllMemOpsDone();

  // before eviction the line is there
  EXPECT_EQ(Exclusive, getState(p0dl1, to_evict));

  // evict
  doread(p0dl1, designAddr(1, 4, 7, 0));
  waitAllMemOpsDone();

  // ensure that the oldest line gets evicted
  // even if it is touched by a spec read
  EXPECT_EQ(Invalid, getState(p0dl1, to_evict));
}

#if 0
// the first test checks that if no cache has data and then one
// cache reads it from memory, then that cache gets set to Exclusive

TEST_F(CacheTest, l1miss_l2miss) {
  doread(p0dl1, 0x100);  // L1 miss, L2 miss
  waitAllMemOpsDone();
  EXPECT_EQ(Exclusive, getState(p0dl1, 0x100));
  EXPECT_EQ(Invalid, getState(p1dl1, 0x100));

  if (p0l2->isJustDirectory()) {
    EXPECT_EQ(Invalid, getState(p0l2, 0x100));
  } else {
    EXPECT_EQ(Exclusive, getState(p0l2, 0x100));
  }
  if (l3) {
    EXPECT_EQ(Exclusive, getState(l3, 0x100));
  }
}

TEST_F(CacheTest, L1miss_l2hit) {
  doread(p0l2, 0x180);  // L2 miss
  waitAllMemOpsDone();
  EXPECT_EQ(Invalid, getState(p0dl1, 0x180));
  EXPECT_EQ(Invalid, getState(p1dl1, 0x180));
  if (p0l2->isJustDirectory()) {
    EXPECT_EQ(Invalid, getState(p0l2, 0x180));
  } else {
    EXPECT_EQ(Exclusive, getState(p0l2, 0x180));
  }
  if (l3) {
    EXPECT_EQ(Exclusive, getState(l3, 0x180));
  }

  dowrite(p0dl1, 0x180);  // L1 miss, L2 hit
  waitAllMemOpsDone();
  EXPECT_EQ(Modified, getState(p0dl1, 0x180));
  EXPECT_EQ(Invalid, getState(p1dl1, 0x180));
  if (p0l2->isJustDirectory()) {
    EXPECT_EQ(Invalid, getState(p0l2, 0x180));
  } else {
    EXPECT_EQ(Exclusive, getState(p0l2, 0x180));
  }
  if (l3) {
    EXPECT_EQ(Exclusive, getState(l3, 0x180));
  }
}

TEST_F(CacheTest, multiple_reqs) {
  // Setup, L2 line (empty in all the others)
  doread(p0l2, 0xF000);  // L2 miss
  waitAllMemOpsDone();
  EXPECT_EQ(Invalid, getState(p0dl1, 0xF000));
  EXPECT_EQ(Invalid, getState(p1dl1, 0xF000));

  if (p0l2->isJustDirectory()) {
    EXPECT_EQ(Invalid, getState(p0l2, 0xF000));
  } else {
    EXPECT_EQ(Exclusive, getState(p0l2, 0xF000));
  }

  if (l3) {
    EXPECT_EQ(Exclusive, getState(l3, 0xF000));
  }

  doread(p0dl1, 0xF000);  // L1 miss, L2 hit
  waitAllMemOpsDone();
  EXPECT_EQ(Exclusive, getState(p0dl1, 0xF000));
  doread(p1dl1, 0xF000);  // L1 miss, L2 hit
  waitAllMemOpsDone();
  EXPECT_EQ(Shared, getState(p0dl1, 0xF000));
  EXPECT_EQ(Shared, getState(p1dl1, 0xF000));
  if (p0l2 == p1l2) {  // Shared L2 conf
    if (p0l2->isJustDirectory()) {
      EXPECT_EQ(Invalid, getState(p0l2, 0xF000));
    } else {
      EXPECT_EQ(Exclusive, getState(p0l2, 0xF000));
    }
  } else {
    if (p0l2->isJustDirectory()) {
      EXPECT_EQ(Invalid, getState(p0l2, 0xF000));
    } else {
      EXPECT_EQ(Shared, getState(p0l2, 0xF000));
    }
    if (p1l2->isJustDirectory()) {
      EXPECT_EQ(Invalid, getState(p1l2, 0xF000));
    } else {
      EXPECT_EQ(Shared, getState(p1l2, 0xF000));
    }
  }
  if (l3) {
    EXPECT_EQ(Exclusive, getState(l3, 0xF000));
  }
}

// the second test checks that if one cache reads a line and then
// a second cache reads that line,  both become shared
TEST_F(CacheTest, Shared_second_test) {
  doread(p0dl1, 0x200);
  waitAllMemOpsDone();
  doread(p1dl1, 0x200);
  waitAllMemOpsDone();

  EXPECT_EQ(Shared, getState(p0dl1, 0x200));
  EXPECT_EQ(Shared, getState(p1dl1, 0x200));
  if (p0l2 == p1l2) {  // Shared L2 conf
    if (p0l2->isJustDirectory()) {
      EXPECT_EQ(Invalid, getState(p0l2, 0x200));
    } else {
      EXPECT_EQ(Exclusive, getState(p0l2, 0x200));
    }
  } else {
    if (p0l2->isJustDirectory()) {
      EXPECT_EQ(Invalid, getState(p0l2, 0x200));
    } else {
      EXPECT_EQ(Shared, getState(p0l2, 0x200));
    }

    if (p1l2->isJustDirectory()) {
      EXPECT_EQ(Invalid, getState(p1l2, 0x200));
    } else {
      EXPECT_EQ(Shared, getState(p1l2, 0x200));
    }
  }
  if (l3) {
    EXPECT_EQ(Exclusive, getState(l3, 0x200));
  }
}

// in the third test, a cache has a write miss that no one else has, and we check if
// the state of that cache goes to modified

TEST_F(CacheTest, Modified_third_test) {
  dowrite(p0dl1, 0x300);
  waitAllMemOpsDone();
  EXPECT_EQ(Modified, getState(p0dl1, 0x300));

  if (p0l2->isJustDirectory()) {
    EXPECT_EQ(Invalid, getState(p0l2, 0x300));
  } else {
    EXPECT_EQ(Exclusive, getState(p0l2, 0x300));
  }
  if (l3) {
    EXPECT_EQ(Exclusive, getState(l3, 0x300));
  }
}

// in the fourth test, a cache reads from memory and becomes exclusive,
// then has a write hit and should go to modified
//
// currently this test fails, and it stays at exclusive
//
TEST_F(CacheTest, Modified_fourth_test) {
  doread(p0dl1, 0x400);
  waitAllMemOpsDone();
  dowrite(p0dl1, 0x400);
  waitAllMemOpsDone();
  EXPECT_EQ(Modified, getState(p0dl1, 0x400));
  if (p0l2->isJustDirectory()) {
    EXPECT_EQ(Invalid, getState(p0l2, 0x400));
  } else {
    EXPECT_EQ(Exclusive, getState(p0l2, 0x400));
  }
  if (l3) {
    EXPECT_EQ(Exclusive, getState(l3, 0x400));
  }
}

// in the fifth test, a cache has a write miss and goes to modified, then
// another cache has a read miss, so the first one becomes owner and the
// second becomes shared
//
TEST_F(CacheTest, Owner_fifth_test) {
  dowrite(p0dl1, 0x500);
  waitAllMemOpsDone();
  doread(p1dl1, 0x500);
  waitAllMemOpsDone();

  EXPECT_EQ(Shared, getState(p0dl1, 0x500));
  EXPECT_EQ(Shared, getState(p1dl1, 0x500));
  if (p0l2 == p1l2) {  // Shared L2 conf
    if (p0l2->isJustDirectory()) {
      EXPECT_EQ(Invalid, getState(p0l2, 0x500));
    } else {
      EXPECT_EQ(Modified, getState(p0l2, 0x500));
    }
    if (l3) {
      EXPECT_EQ(Exclusive, getState(l3, 0x500));
    }
  } else {
    if (p0l2->isJustDirectory()) {
      EXPECT_EQ(Invalid, getState(p0l2, 0x500));
    } else {
      EXPECT_EQ(Shared, getState(p0l2, 0x500));
    }
    if (p1l2->isJustDirectory()) {
      EXPECT_EQ(Shared, getState(p1l2, 0x500));
    } else {
      EXPECT_EQ(Shared, getState(p1l2, 0x500));
    }
    if (l3) {
      EXPECT_EQ(Modified, getState(l3, 0x500));
    }
  }
}

// in the sixth test, a cache has a read miss, then the other
// cache has a write miss, invalidating the first cache and becoming modified
// itself
//
// The test currently passes, but it should be noted that the cache line does
// not actually enter an invalid state, it is just assigned a null pointer
TEST_F(CacheTest, Invalid_sixth_test) {
  doread(p0dl1, 0x600);
  waitAllMemOpsDone();
  dowrite(p1dl1, 0x600);
  waitAllMemOpsDone();

  EXPECT_EQ(Invalid, getState(p0dl1, 0x600));
  EXPECT_EQ(Modified, getState(p1dl1, 0x600));
  if (p1l2->isJustDirectory()) {
    EXPECT_EQ(Invalid, getState(p1l2, 0x600));
  } else {
    EXPECT_EQ(Exclusive, getState(p1l2, 0x600));
  }
  if (l3) {
    EXPECT_EQ(Exclusive, getState(l3, 0x600));
  }

  dowrite(p0dl1, 0x600);
  waitAllMemOpsDone();
  EXPECT_EQ(Modified, getState(p0dl1, 0x600));
  EXPECT_EQ(Invalid, getState(p1dl1, 0x600));
  if (p0l2 == p1l2) {  // Shared L2 conf
    if (p0l2->isJustDirectory()) {
      EXPECT_EQ(Invalid, getState(p0l2, 0x600));
    } else {
      EXPECT_EQ(Modified, getState(p0l2, 0x600));
    }
    if (l3) {
      EXPECT_EQ(Exclusive, getState(l3, 0x600));
    }
  } else {
    EXPECT_EQ(Invalid, getState(p1l2, 0x600));
    if (p0l2->isJustDirectory()) {
      EXPECT_EQ(Invalid, getState(p0l2, 0x600));
    } else {
      EXPECT_EQ(Exclusive, getState(p0l2, 0x600));
    }
    if (l3) {
      EXPECT_EQ(Modified, getState(l3, 0x600));
    }
  }
}

// in the seventh test, a cache has a read miss and then the other cache has
// a read miss, so then they are both shared. Then the second cache does a
// write and becomes modified, while the first cache is invalidated.

TEST_F(CacheTest, Invalid_seventh_test) {
  doread(p0dl1, 0x700);
  waitAllMemOpsDone();
  EXPECT_EQ(Exclusive, getState(p0dl1, 0x700));
  doread(p1dl1, 0x700);
  waitAllMemOpsDone();
  EXPECT_EQ(Shared, getState(p0dl1, 0x700));
  EXPECT_EQ(Shared, getState(p1dl1, 0x700));
  if (l3) {
    if (p0l2 == p1l2) {  // Shared L2 conf
      if (p1l2->isJustDirectory()) {
        EXPECT_EQ(Invalid, getState(p1l2, 0x700));
      } else {
        EXPECT_EQ(Exclusive, getState(p1l2, 0x700));
      }
      EXPECT_EQ(Exclusive, getState(l3, 0x700));
    } else {
      if (p0l2->isJustDirectory()) {
        EXPECT_EQ(Invalid, getState(p0l2, 0x700));
      } else {
        EXPECT_EQ(Shared, getState(p0l2, 0x700));
      }
      if (p1l2->isJustDirectory()) {
        EXPECT_EQ(Invalid, getState(p1l2, 0x700));
      } else {
        EXPECT_EQ(Shared, getState(p1l2, 0x700));
      }
      EXPECT_EQ(Exclusive, getState(l3, 0x700));
    }
  }
  dowrite(p1dl1, 0x700);
  waitAllMemOpsDone();

  EXPECT_EQ(Invalid, getState(p0dl1, 0x700));
  EXPECT_EQ(Modified, getState(p1dl1, 0x700));
  if (p1l2->isJustDirectory()) {
    EXPECT_EQ(Invalid, getState(p1l2, 0x700));
  } else {
    EXPECT_EQ(Exclusive, getState(p1l2, 0x700));
  }
  if (l3) {
    EXPECT_EQ(Exclusive, getState(l3, 0x700));
  }
}

// in the eighth test, a cache has a write miss, becoming modified. Then
// another cache has a read miss, so that the first one becomes owner
// and the second becomes shared. Then the second one is written to becoming
// Modified, invalidating the owner
//
// This test fails. The first cache does transition from Owner to Invalid,
// but the second cache fails to go from shared to modified.
TEST_F(CacheTest, Invalid_eighth_test) {
  dowrite(p1dl1, 0x800);
  waitAllMemOpsDone();
  doread(p0dl1, 0x800);
  waitAllMemOpsDone();
  EXPECT_EQ(Shared, getState(p1dl1, 0x800));
  EXPECT_EQ(Shared, getState(p0dl1, 0x800));
  dowrite(p0dl1, 0x800);
  waitAllMemOpsDone();

  EXPECT_EQ(Invalid, getState(p1dl1, 0x800));
  EXPECT_EQ(Modified, getState(p0dl1, 0x800));
  if (p0l2 == p1l2) {  // Shared L2 conf
    if (p0l2->isJustDirectory()) {
      EXPECT_EQ(Invalid, getState(p0l2, 0x800));
    } else {
      EXPECT_EQ(Modified, getState(p0l2, 0x800));
    }
    if (l3) {
      EXPECT_EQ(Exclusive, getState(l3, 0x800));
    }
  } else {
    if (p0l2->isJustDirectory()) {
      EXPECT_EQ(Invalid, getState(p0l2, 0x800));
    } else {
      EXPECT_EQ(Shared, getState(p0l2, 0x800));
    }
    if (l3) {
      EXPECT_EQ(Modified, getState(l3, 0x800));
    }
  }
}

// in the ninth test, a cache does a read to become exclusive. Then another
// cache does a write which makes it modified and the first cache invalid
//
TEST_F(CacheTest, Invalid_ninth_test) {
  doread(p0dl1, 0x900);
  waitAllMemOpsDone();
  dowrite(p1dl1, 0x900);
  waitAllMemOpsDone();
  EXPECT_EQ(Invalid, getState(p0dl1, 0x900));
  EXPECT_EQ(Modified, getState(p1dl1, 0x900));
  if (p0l2 == p1l2) {  // Shared L2 conf
    if (p1l2->isJustDirectory()) {
      EXPECT_EQ(Invalid, getState(p1l2, 0x900));
    } else {
      EXPECT_EQ(Exclusive, getState(p1l2, 0x900));
    }
    if (l3) {
      EXPECT_EQ(Exclusive, getState(l3, 0x900));
    }
  } else {
    EXPECT_EQ(Invalid, getState(p0l2, 0x900));
    if (p1l2->isJustDirectory()) {
      EXPECT_EQ(Invalid, getState(p1l2, 0x900));
    } else {
      EXPECT_EQ(Exclusive, getState(p1l2, 0x900));
    }
    if (l3) {
      EXPECT_EQ(Exclusive, getState(l3, 0x900));
    }
  }

  doread(p1dl1, 0x900);
  waitAllMemOpsDone();
  EXPECT_EQ(Invalid, getState(p0dl1, 0x900));
  EXPECT_EQ(Modified, getState(p1dl1, 0x900));
  if (p0l2 == p1l2) {  // Shared L2 conf
    if (p1l2->isJustDirectory()) {
      EXPECT_EQ(Invalid, getState(p1l2, 0x900));
    } else {
      EXPECT_EQ(Exclusive, getState(p1l2, 0x900));
    }
    if (l3) {
      EXPECT_EQ(Exclusive, getState(l3, 0x900));
    }
  } else {
    EXPECT_EQ(Invalid, getState(p0l2, 0x900));
    if (p1l2->isJustDirectory()) {
      EXPECT_EQ(Invalid, getState(p1l2, 0x900));
    } else {
      EXPECT_EQ(Exclusive, getState(p1l2, 0x900));
    }
    if (l3) {
      EXPECT_EQ(Exclusive, getState(l3, 0x900));
    }
  }

  doread(p0dl1, 0x900);
  waitAllMemOpsDone();
  EXPECT_EQ(Shared, getState(p0dl1, 0x900));
  EXPECT_EQ(Shared, getState(p1dl1, 0x900));
  if (p0l2 == p1l2) {  // Shared L2 conf
    if (p1l2->isJustDirectory()) {
      EXPECT_EQ(Invalid, getState(p1l2, 0x900));
    } else {
      EXPECT_EQ(Modified, getState(p1l2, 0x900));
    }
    if (l3) {
      EXPECT_EQ(Exclusive, getState(l3, 0x900));
    }
  } else {
    if (p0l2->isJustDirectory()) {
      EXPECT_EQ(Invalid, getState(p0l2, 0x900));
    } else {
      EXPECT_EQ(Shared, getState(p0l2, 0x900));
    }
    if (p1l2->isJustDirectory()) {
      EXPECT_EQ(Invalid, getState(p1l2, 0x900));
    } else {
      EXPECT_EQ(Shared, getState(p1l2, 0x900));
    }
    if (l3) {
      EXPECT_EQ(Modified, getState(l3, 0x900));
    }
  }

  dowrite(p0dl1, 0x900);
  waitAllMemOpsDone();
  EXPECT_EQ(Invalid, getState(p1dl1, 0x900));
  EXPECT_EQ(Modified, getState(p0dl1, 0x900));
  if (p0l2 == p1l2) {  // Shared L2 conf
    if (p1l2->isJustDirectory()) {
      EXPECT_EQ(Invalid, getState(p1l2, 0x900));
    } else {
      EXPECT_EQ(Modified, getState(p1l2, 0x900));
    }
    if (l3) {
      EXPECT_EQ(Exclusive, getState(l3, 0x900));
    }
  } else {
    if (p0l2->isJustDirectory()) {
      EXPECT_EQ(Invalid, getState(p0l2, 0x900));  // It could be E
    } else {
      EXPECT_EQ(Shared, getState(p0l2, 0x900));  // It could be E
    }
    EXPECT_EQ(Invalid, getState(p1l2, 0x900));
    if (l3) {
      EXPECT_EQ(Modified, getState(l3, 0x900));
    }
  }
}

TEST_F(CacheTest, justDirectory) {
  printf("BEGIN L2 directory test\n");
  doread(p0dl1, 0x900);
  waitAllMemOpsDone();
  dowrite(p1dl1, 0x900);
  waitAllMemOpsDone();
  EXPECT_EQ(Invalid, getState(p0dl1, 0x900));
  EXPECT_EQ(Modified, getState(p1dl1, 0x900));
}

#if 0
TEST_F(CacheTest,prefetch1){
  printf("BEGIN prefetch is not in the way\n");
  waitAllMemOpsDone();

  Time_t start;
  Time_t pref1;
  Time_t pref2;

  // Calibration
  start = globalClock;
  doread(p0dl1,0xa00);
  waitAllMemOpsDone();
  Time_t nopref = globalClock - start;

  // Prefetch ahead 2 cycles
  start = globalClock;
  doprefetch(p0dl1,0xb00);
  EventScheduler::advanceClock();
  EventScheduler::advanceClock();
  doread(p0dl1,0xb00);
  waitAllMemOpsDone();
  pref2 = globalClock - start;
  EXPECT_EQ(nopref,pref2); // prefetch started ahead 2 cycles

  // Prefetch ahead 1 cycles
  start = globalClock;
  doprefetch(p0dl1,0xc00);
  EventScheduler::advanceClock();
  doread(p0dl1,0xc00);
  waitAllMemOpsDone();
  pref1 = globalClock - start;
  EXPECT_EQ(nopref,pref1); // prefetch started ahead 2 cycles

  // Prefetch ahead 2 cycles
  start = globalClock;
  doread(p0dl1,0xd00);
  EventScheduler::advanceClock();
  EventScheduler::advanceClock();
  doprefetch(p0dl1,0xd00);
  waitAllMemOpsDone();
  pref2 = globalClock - start;
  EXPECT_EQ(nopref,pref2); // prefetch started ahead 2 cycles

  // Prefetch ahead 1 cycles
  start = globalClock;
  doread(p0dl1,0xe00);
  EventScheduler::advanceClock();
  doprefetch(p0dl1,0xe00);
  waitAllMemOpsDone();
  pref1 = globalClock - start;
  EXPECT_EQ(nopref,pref1); // prefetch started ahead 2 cycles

  // Prefetch ahead 0 cycles
  start = globalClock;
  doprefetch(p0dl1,0xf00);
  doread(p0dl1,0xf00);
  waitAllMemOpsDone();
  pref1 = globalClock - start;
  EXPECT_EQ(nopref,pref1); // prefetch started ahead 2 cycles

  printf("nopred=%d pref2=%d\n",nopref, pref2);
}

TEST_F(CacheTest,prefetch2) {
  printf("BEGIN prefetch L1/L2 mix\n");
  waitAllMemOpsDone();

  Time_t start;
  Time_t pref1;
  Time_t pref2;

  // Calibration
  start = globalClock;
  doread(p0dl1,0x100a00);
  waitAllMemOpsDone();
  Time_t nopref = globalClock - start;

  // Prefetch ahead 2 cycles
  start = globalClock;
  doprefetch(p0l2,0x100b00);
  EventScheduler::advanceClock();
  EventScheduler::advanceClock();
  doread(p0dl1,0x100b00);
  waitAllMemOpsDone();
  pref2 = globalClock - start;
  EXPECT_EQ(nopref,pref2); // prefetch started ahead 2 cycles

  // Prefetch ahead 1 cycles
  start = globalClock;
  doprefetch(p0l2,0x100c00);
  EventScheduler::advanceClock();
  doread(p0dl1,0x100c00);
  waitAllMemOpsDone();
  pref1 = globalClock - start;
  EXPECT_EQ(nopref,pref1); // prefetch started ahead 2 cycles

  // Prefetch ahead 2 cycles
  start = globalClock;
  doread(p0dl1,0x100d00);
  EventScheduler::advanceClock();
  EventScheduler::advanceClock();
  doprefetch(p0l2,0x100d00);
  waitAllMemOpsDone();
  pref2 = globalClock - start;
  EXPECT_EQ(nopref,pref2); // prefetch started ahead 2 cycles

  // Prefetch ahead 1 cycles
  start = globalClock;
  doread(p0dl1,0x100e00);
  EventScheduler::advanceClock();
  doprefetch(p0l2,0x100e00);
  waitAllMemOpsDone();
  pref1 = globalClock - start;
  EXPECT_EQ(nopref,pref1); // prefetch started ahead 2 cycles

  // Prefetch ahead 0 cycles
  start = globalClock;
  doprefetch(p0l2,0x100f00);
  doread(p0dl1,0x100f00);
  waitAllMemOpsDone();
  pref1 = globalClock - start;
  EXPECT_EQ(nopref,pref1); // prefetch started ahead 2 cycles

  printf("nopred=%d pref2=%d\n",nopref, pref2);
}
#endif

TEST_F(CacheTest, l2_bw) {
  printf("BEGIN L2 BW test\n");

  Time_t start;
  int    conta;
  double t;

  start = globalClock;
  conta = 0;
  for (int i = 0; i < 32768; i += 128) {
    doread(p0l2, 0x1000 + i);  // Fill L2
    conta++;
  }
  waitAllMemOpsDone();
  Time_t l2filltime = globalClock - start;
  t                 = ((double)l2filltime) / conta;
  printf("L2 BW miss test %lld (%g cycles/l2_miss)\n", l2filltime, t);

  start = globalClock;
  conta = 0;
  for (int i = 0; i < 32768 / 2; i += 128) {
    doread(p0dl1, 0x1000 + i);
    conta++;
  }
  waitAllMemOpsDone();
  Time_t l1_rdtime = globalClock - start;
  t                = ((double)l1_rdtime) / conta;
  //printf("L2 BW rd test %lld (%g cycles/l1_rd)\n", l1_rdtime, t);

  start = globalClock;
  conta = 0;
  for (int i = 32768 / 2; i < 32768; i += 128) {
    dowrite(p0dl1, 0x1000 + i);
    conta++;
  }
  waitAllMemOpsDone();
  Time_t l1_wrtime = globalClock - start;
  t                = ((double)l1_wrtime) / conta;
  printf("L2 BW wr test %lld (%g cycles/l1_wr)\n", l1_wrtime, t);

  printf("END L2 BW test\n");
}
#endif