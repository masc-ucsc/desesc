// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "resource.hpp"

#include <random>

#include "config.hpp"
#include "gmemory_system.hpp"
#include "gmock/gmock.h"
#include "gprocessor.hpp"
#include "gtest/gtest.h"
#include "memory_system.hpp"
#include "oooprocessor.hpp"

class LSQ;
class PortGeneric;
class Dinst;
class MemObj;
class Cluster;
class Resource;

std::shared_ptr<Gmemory_system> global_mem_sys_p0 = nullptr;

static void setup_config() {
  std::ofstream file;

  file.open("cachecore.toml");

  file << "[soc]\n"
          "core = [\"c0\",\"c0\"]\n"
          "[c0]\n"
          "type  = \"ooo\"\n"
          "frequency_mhz = 1000\n"
          "do_random_transients = false\n"
          "fetch_align = true \n"
          "trace_align = false \n"
          "fetch_one_line = true \n"
          "max_bb_cycle = 1 \n"
          "prefetcher = \"pref_opt\"\n"
          "caches        = true\n"
          "scb_size      = 16\n"
          "storeset_size = 8192 \n"
          "ldq_late_alloc = false\n"
          "dl1           = \"dl1_cache DL1\"\n"
          "il1           = \"dl1_cache IL1\"\n"
          "cluster = [\"aunit\", \"bunit\", \"cunit\", \"munit\"]\n"
          "num_regs   = 256 \n"
          "inter_cluster_lat    = 0 \n"
          "cluster_scheduler    = \"RoundRobin\"\n"
          "max_branches         = 30\n"
          "drain_on_miss        = false\n"
          "commit_delay         = 2\n"
          "replay_serialize_for = 32\n"
          "scoore_serialze      = true\n"
          "fetch_width  = 8\n"
          "issue_width  = 8\n"
          "retire_width = 8\n"
          "rob_size     = 320\n"
          "decode_delay = 4\n"
          "rename_delay = 2\n"
          "ftq_size     = 12\n"
          "instq_size   = 16\n"
          "smt = 1\n"
          "memory_replay = true\n"
          "st_fwd_delay  = 2\n"
          "ldq_size      = 96\n"
          "stq_size      = 64\n"
          "stq_late_alloc = false\n"
          "scoore_serialize = true\n"

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
          "victim        = true\n"
          "coherent      = true\n"
          "inclusive     = false\n"
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
          "victim        = true\n"
          "coherent      = true\n"
          "inclusive     = false\n"
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

          "[aunit]\n"
          "win_size   = 16\n"
          "sched_num  = 4\n"
          "sched_occ  = 1\n"
          "sched_lat  = 1\n"
          "recycle_at  = \"executed\"\n"
          "num_regs   = 64\n"
          "late_alloc = false\n"
          "iAALU      = \"alu0\"\n"
          "iRALU      = \"alu0\"\n"

          "[bunit]\n"
          "win_size   = 16\n"
          "sched_num  = 4\n"
          "sched_occ  = 1\n"
          "sched_lat  = 1\n"
          "recycle_at  = \"executed\"\n"
          "num_regs   = 64\n"
          "late_alloc = false\n"
          "iBALU_LBRANCH = \"balu\"\n"
          "iBALU_LJUMP   = \"balu\"\n"
          "iBALU_LCALL   = \"balu\"\n"
          "iBALU_RBRANCH = \"balu\"\n"
          "iBALU_RJUMP   = \"balu\"\n"
          "iBALU_RCALL   = \"balu\"\n"
          "iBALU_RET     = \"balu\"\n"

          "[cunit]\n"
          "win_size   = 16\n"
          "sched_num  = 4\n"
          "sched_occ  = 1\n"
          "sched_lat  = 1\n"
          "recycle_at  = \"executed\"\n"
          "num_regs   = 64\n"
          "late_alloc = false\n"
          "iCALU_FPMULT  = \"calu2\"\n"
          "iCALU_FPDIV   = \"calu2\"\n"
          "iCALU_FPALU   = \"calu2\"\n"
          "iCALU_MULT    = \"calu2\"\n"
          "iCALU_DIV     = \"calu2\"\n"

          "[munit]\n"
          "win_size   = 16\n"
          "sched_num  = 4\n"
          "sched_occ  = 1\n"
          "sched_lat  = 1\n"
          "recycle_at  = \"executed\"\n"
          "num_regs   = 64\n"
          "late_alloc = false\n"
          "iLALU_LD      = \"lsu\"\n"
          "iSALU_ST      = \"lsu\"\n"
          "iSALU_LL      = \"lsu\"\n"
          "iSALU_SC      = \"lsu\"\n"
          "iSALU_ADDR    = \"lsu\"\n"

          "[pref_opt]\n"
          "type       = \"stride\"\n"
          "degree     = 10\n"
          "distance   = 0\n";

  file.close();
}

void initialize() {
  static bool pluggedin = false;
  if (!pluggedin) {
    setup_config();
    Config::init("cachecore.toml");
    global_mem_sys_p0 = std::make_shared<Memory_system>(0);
    pluggedin         = true;
  }
}

class resource_test : public ::testing::Test {
protected:
  FULoad                       *ful;
  LSQ                          *lsq;
  GProcessor                   *gproc;
  std::shared_ptr<Prefetcher>   pref;
  std::shared_ptr<Store_buffer> scb;
  std::shared_ptr<Cluster>      cls;
  std::shared_ptr<StoreSet>     ss;
  Opcode                        type;
  PortGeneric                  *aGen;
  TimeDelta_t                   lsdelay;
  TimeDelta_t                   l;
  int32_t                       size;
  int32_t                       id;
  const char                   *cad;

  void SetUp() override { initialize(); }

  void TearDown() override { delete ful; }

public:
  void setupFULoad(const std::string &clusterName) {
    size    = 256 * 1024;
    type    = Opcode::iLALU_LD;
    gproc   = new OoOProcessor(global_mem_sys_p0, 0);
    cls     = std::make_shared<ExecutingCluster>(clusterName, 0, 0);
    lsdelay = 0;
    aGen    = PortGeneric::create("ld_alu_0", 2, 1);
    lsq     = gproc->getLSQ();
    scb     = gproc->ref_SCB();
    pref    = gproc->ref_prefetcher();
    ss      = gproc->ref_SS();
    id      = 0;
    l       = lsdelay;

    ful = new FULoad(Opcode::iLALU_LD, cls, aGen, lsq, ss, pref, scb, lsdelay, l, global_mem_sys_p0, size, id, "specld");
  }
  Dinst *createLDInstAddr(Addr_t addr) {
    auto *st_inst
        = Dinst::create(Instruction(Opcode::iLALU_LD, RegType::LREG_R1, RegType::LREG_R2, RegType::LREG_R3, RegType::LREG_R4),
                        0xdeaddead  // pc
                        ,
                        addr,
                        0,
                        true);
    return st_inst;
  }
  Addr_t randomAddrGen() {
    Addr_t                          addr = 0x00fbc0;
    std::random_device              rd;
    std::mt19937                    gen(rd());
    std::uniform_int_distribution<> dis(1, 100);
    int                             randomNumber = dis(gen);
    return addr + (uint64_t)randomNumber;
  }
};

TEST_F(resource_test, can_issue_an_load_instruction) {
  this->setupFULoad("munit");
  Addr_t any_addr = 0x111;
  auto  *ld_inst  = this->createLDInstAddr(any_addr);
  EXPECT_EQ(ful->canIssue(ld_inst), 0);
  EXPECT_EQ(true, true);
}

TEST_F(resource_test, can_issue_multiple_load_instruction) {
  this->setupFULoad("munit");
  for (auto i = 0; i < 16; i++) {
    auto *ld_inst = this->createLDInstAddr(this->randomAddrGen());
    if (i == 0) {
      EXPECT_EQ(ful->canIssue(ld_inst), 0);
    } else {
      EXPECT_EQ(ful->canIssue(ld_inst), 5);
    }
  }
}
