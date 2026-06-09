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

#include "callback.hpp"
#include "ccache.hpp"
#include "config.hpp"
#include "dinst.hpp"
#include "fmt/format.h"
#include "gprocessor.hpp"
#include "gtest/gtest.h"
#include "iassert.hpp"
#include "instruction.hpp"
#include "memobj.hpp"
#include "memory_system.hpp"
#include "memrequest.hpp"
#include "memstruct.hpp"
#include "report.hpp"

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

currState getState(CCache* cache, Addr_t addr) {
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

void rdDone(Dinst* dinst) {
  // printf("rddone @%lld\n", (long long)globalClock);

  rd_pending--;
  dinst->scrap();
}

void wrDone(Dinst* dinst) {
  // printf("wrdone @%lld\n", (long long)globalClock);

  wr_pending--;
  dinst->scrap();
}

static void waitAllMemOpsDone() {
  while (rd_pending || wr_pending) {
    EventScheduler::advanceClock();
  }
}

typedef CallbackFunction1<Dinst*, &rdDone> rdDoneCB;
typedef CallbackFunction1<Dinst*, &wrDone> wrDoneCB;

// non-speculative read by default
static void doread(MemObj* cache, Addr_t addr, bool spec = false) {
  num_operations++;

  auto* ldClone
      = Dinst::create(Instruction(Opcode::iLALU_LD, RegType::LREG_R1, RegType::LREG_R2, RegType::LREG_R3, RegType::LREG_R4),
                      0xdeaddead  // pc
                      ,
                      addr,
                      0,
                      true);

  while (cache->isBusy(addr)) {
    EventScheduler::advanceClock();
  }

  rdDoneCB* cb = rdDoneCB::create(ldClone, ldClone->getID());
  // printf("rd %x @%lld\n", (unsigned int)addr, (long long)globalClock);

  if (spec) {
    MemRequest::sendSpecReqDL1Read(cache, ldClone->has_stats(), ldClone->getAddr(), ldClone->getPC(), /*Dinst *dinst*/ 0, cb);
  } else {
    MemRequest::sendSafeReqDL1Read(cache, ldClone->has_stats(), ldClone->getAddr(), ldClone->getPC(), /*Dinst *dinst*/ 0, cb);
  }
  rd_pending++;
}

static void doprefetch(MemObj* cache, Addr_t addr) {
  num_operations++;

  auto* ldClone
      = Dinst::create(Instruction(Opcode::iLALU_LD, RegType::LREG_R5, RegType::LREG_R6, RegType::LREG_R7, RegType::LREG_R8),
                      0xbeefbeef  // pc
                      ,
                      addr,
                      0,
                      true);

  while (cache->isBusy(addr)) {
    EventScheduler::advanceClock();
  }

  rdDoneCB* cb = rdDoneCB::create(ldClone, ldClone->getID());
  // printf("rd %x @%lld\n", (unsigned int)addr, (long long)globalClock);

  cache->tryPrefetch(ldClone->has_stats(), ldClone->getAddr(), 1, 0xF00D, ldClone->getPC(), cb);
  rd_pending++;
}

static void dowrite(MemObj* cache, Addr_t addr) {
  num_operations++;

  auto* stClone
      = Dinst::create(Instruction(Opcode::iSALU_ST, RegType::LREG_R5, RegType::LREG_R6, RegType::LREG_R7, RegType::LREG_R8),
                      0x200  // pc
                      ,
                      addr,
                      0,
                      true);

  while (cache->isBusy(addr)) {
    EventScheduler::advanceClock();
  }

  wrDoneCB* cb = wrDoneCB::create(stClone, stClone->getID());
  // printf("wr %x @%lld\n", (unsigned int)addr, (long long)globalClock);

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

Gmemory_system* gms_p0 = nullptr;
Gmemory_system* gms_p1 = nullptr;

static void setup_config() {
  std::ofstream file;

  file.open("cachecore.toml");

  file << "[soc]\n"
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
          "lower_level = \"\"\n";

  file.close();
}

void initialize() {
  static bool pluggedin = false;
  if (!pluggedin) {
    setup_config();

    Report::init();
    Config::init("cachecore.toml");

    gms_p0    = new Memory_system(0);
    gms_p1    = new Memory_system(1);
    pluggedin = true;
#ifdef DEBUG_CALLPATH
    forcemsgdump = true;
#endif

    Config::exit_on_error();
    EventScheduler::advanceClock();
  }
}

CCache* getDL1(Gmemory_system* gms) {
  MemObj* dl1 = gms->getDL1();
  if (dl1->get_type() != "cache") {
    fmt::print("ERROR: The first level must be a cache {}\n", dl1->get_type());
    exit(-1);
  }

  CCache* cdl1 = static_cast<CCache*>(dl1);
  return cdl1;
}

CCache* getL3(MemObj* L2) {
  MRouter* router2 = L2->getRouter();
  MemObj*  L3      = router2->getDownNode();
  if (L3->get_type() != "cache") {
    return nullptr;
  }

  CCache* l3c = static_cast<CCache*>(L3);
  return l3c;
}

CCache* getL2(MemObj* P0DL1) {
  MRouter* router = P0DL1->getRouter();
  MemObj*  L2     = router->getDownNode();
  I(L2->get_type() == "cache");

  CCache* l2c = static_cast<CCache*>(L2);
  // l2c->setNeedsCoherence();
  return l2c;
}

class CacheTest : public testing::Test {
protected:
  CCache*      p0dl1;
  CCache*      p0l2;
  CCache*      p1l2;
  CCache*      l3;
  CCache*      p1dl1;
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

