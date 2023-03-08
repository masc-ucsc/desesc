// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "store_buffer.hpp"

#include "config.hpp"
#include "gprocessor.hpp"
#include "gmemory_system.hpp"
#include "memory_system.hpp"
#include "gtest/gtest.h"

std::shared_ptr<Gmemory_system> global_mem_sys_p0 = nullptr;

static void setup_config() {
  std::ofstream file;

  file.open("cachecore.toml");

  file << "[soc]\n"
          "core = [\"c0\",\"c0\"]\n"
          "[c0]\n"
          "type  = \"ooo\"\n"
          "caches        = true\n"
          "scb_size      = 16\n"
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
          "victim        = false\n"
          "coherent      = true\n"
          "inclusive     = true\n"
          "directory     = false\n"
          "nlp_distance = 2\n"
          "nlp_degree   = 1       # 0 disabled\n"
          "nlp_stride   = 1\n"
          "drop_prefetch = true\n"
          "prefetch_degree = 0    # 0 disabled\n"
          "mega_lines1K    = 8    # 8 lines touched, triggers mega/carped prefetch\n"
          "lower_level = \"privl2 L2 sharedby 2\"\n"
          ;

  file.close();
}

void initialize() {
  static bool pluggedin = false;
  if (!pluggedin) {
    setup_config();
    Config::init("cachecore.toml");
    global_mem_sys_p0 = std::make_shared<Memory_system>(0);
    pluggedin = true;
  }
}
#define PR(a) printf("%s", a)
class Store_buffer_test : public ::testing::Test {
protected:
  Store_buffer *sb;
  void SetUp() override { initialize();}
  void TearDown() override {
    delete sb;
    // Graph_library::sync_all();
  }

public:
void setupStoreBuffer() {
    sb  = new Store_buffer(0, global_mem_sys_p0);
    
  }
Dinst *createStInst() {
  Addr_t addr    = 0x200;
  auto  *st_inst = Dinst::create(Instruction(iSALU_ST, LREG_R1, LREG_R2, LREG_R3, LREG_R4),
                                0xdeaddead  // pc
                                ,
                                addr,
                                0,
                                true);
  return st_inst;
}

Addr_t sb_calc_line(Addr_t addr) const { 
  size_t line_size_addr_bits = 6;
  return addr >> line_size_addr_bits; 
}
Addr_t sb_calc_offset(Addr_t addr) const { 
  size_t line_size_mask = 63;
  return addr & line_size_mask; 
}

};

/* The first test checks that if the store buffer line can accept a new store instruction,
by adding a store instruction to the store buffer line and then checking a data_forwarding load */
TEST_F(Store_buffer_test, store_buf_line_is_load_forward) {
  this->setupStoreBuffer();
  Store_buffer_line sbline;

  Addr_t st_addr      = this->createStInst()->getAddr();
  auto   st_addr_line = this->sb_calc_line(st_addr);

  sbline.init(64, st_addr_line);
  sbline.add_st(this->sb_calc_offset(st_addr));
  EXPECT_EQ(sbline.is_ld_forward(this->sb_calc_offset(st_addr)), true);
  EXPECT_EQ(sbline.is_ld_forward(this->sb_calc_offset(st_addr)+0x11), false);

}

/* The second test checks that if the store buffer can accept a new store instruction,
by adding a store instruction to the store buffer and then checking a data_forwarding load */
TEST_F(Store_buffer_test, store_buf_is_load_forward) {

  this->setupStoreBuffer();
  auto  *st_inst = this->createStInst();
  Addr_t any_addr = 0x111;
  EXPECT_EQ(sb->can_accept_st(any_addr), true);
  EXPECT_EQ(sb->can_accept_st(this->createStInst()->getAddr()), true);

  // is_ld_forward false, because st_inst not yet added to sb
  EXPECT_EQ(sb->is_ld_forward(this->createStInst()->getAddr()), false);

  sb->add_st(st_inst);
  // is_ld_forward true, because st_inst has been added to sb
  EXPECT_EQ(sb->is_ld_forward(this->createStInst()->getAddr()), true);
  sb->ownership_done(st_inst->getAddr());
}