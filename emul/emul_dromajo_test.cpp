// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "emul_dromajo.hpp"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <net/if.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <fstream>

// #define REGRESS_COSIM 1
#ifdef REGRESS_COSIM
#include "dromajo_cosim.h"
#endif

#include "gmock/gmock.h"
#include "gtest/gtest.h"

class Emul_Dromajo_test : public ::testing::Test {
protected:
  std::shared_ptr<Emul_dromajo> dromajo_ptr;

  void SetUp() override {
    std::ofstream file;
    file.open("emul_dromajo_test.toml");

    file << "[soc]\n";
    file << "core = \"c0\"\n";
    file << "emul = [\"drom_emu\"]\n";
    file << "\n[drom_emu]\n";
    file << "num = \"1\"\n";
    file << "type = \"dromajo\"\n";
    file << "rabbit = 0\n";
    file << "detail = 1e6\n";
    file << "time = 2e6\n";
    file << "bench=\"conf/dhrystone.riscv\"\n";
    file.close();

    Config::init("emul_dromajo_test.toml");

    dromajo_ptr = std::make_shared<Emul_dromajo>();
  }

  void TearDown() override {
    // Graph_library::sync_all();
  }
};

TEST_F(Emul_Dromajo_test, dhrystone_test) {
  EXPECT_NE(dromajo_ptr, nullptr);

  dromajo_ptr->skip_rabbit(0, 606);
  Dinst *dinst = dromajo_ptr->peek(0);  // c.j
  EXPECT_EQ(0x0000000080002c1a, dinst->getPC());
  const Instruction *inst = dinst->getInst();
  EXPECT_TRUE(inst->isJump());
  EXPECT_FALSE(inst->hasSrc1Register());
  EXPECT_FALSE(inst->hasDstRegister());
  dinst->scrap();

  dromajo_ptr->skip_rabbit(0, 55);  // c.bnez
  dinst = dromajo_ptr->peek(0);
  EXPECT_EQ(0x00000000800025d8, dinst->getPC());
  inst = dinst->getInst();
  EXPECT_EQ(10, inst->getSrc1());
  EXPECT_EQ(0x00000000800025e8, dinst->getAddr());
  EXPECT_TRUE(inst->isBranch());
  dinst->scrap();

  dromajo_ptr->skip_rabbit(0, 3);
  dinst = dromajo_ptr->peek(0);  // csrr
  EXPECT_EQ(0x0000000080002aaa, dinst->getPC());
  inst = dinst->getInst();
  EXPECT_EQ(15, inst->getDst1());
  dinst->scrap();

  dromajo_ptr->skip_rabbit(0, 197);
  dinst = dromajo_ptr->peek(0);  // bge
  EXPECT_EQ(0x0000000080002094, dinst->getPC());
  EXPECT_EQ(0x00000000800020a4, dinst->getAddr());
  inst = dinst->getInst();
  EXPECT_EQ(0, inst->getSrc1());
  EXPECT_EQ(10, inst->getSrc2());
  EXPECT_FALSE(inst->hasDstRegister());
  EXPECT_TRUE(inst->isBranch());
  dinst->scrap();

  dromajo_ptr->skip_rabbit(0, 6);  // ret
  dinst = dromajo_ptr->peek(0);
  EXPECT_EQ(0x00000000800020ae, dinst->getPC());
  inst = dinst->getInst();
  EXPECT_TRUE(inst->isFuncRet());
  dinst->scrap();

  dromajo_ptr->skip_rabbit(0, 2);  // addi
  dinst = dromajo_ptr->peek(0);
  EXPECT_EQ(0x0000000080002b38, dinst->getPC());
  inst = dinst->getInst();
  EXPECT_EQ(8, inst->getSrc1());
  EXPECT_EQ(12, inst->getDst1());
  EXPECT_TRUE(inst->isALU());
  dinst->scrap();

  dromajo_ptr->skip_rabbit(0, 5);  // sw
  dinst = dromajo_ptr->peek(0);
  EXPECT_EQ(0x0000000080002b46, dinst->getPC());
  EXPECT_EQ(0x0000000080025658, dinst->getAddr());
  inst = dinst->getInst();
  EXPECT_EQ(8, inst->getSrc1());
  EXPECT_EQ(15, inst->getSrc2());
  EXPECT_FALSE(inst->hasDstRegister());
  EXPECT_TRUE(inst->isStore());
  dinst->scrap();

  dromajo_ptr->skip_rabbit(0, 4);  // c.sw
  dinst = dromajo_ptr->peek(0);
  EXPECT_EQ(0x0000000080002004, dinst->getPC());
  EXPECT_EQ(0x0000000080025658, dinst->getAddr());
  inst = dinst->getInst();
  EXPECT_EQ(12, inst->getSrc1());
  EXPECT_EQ(11, inst->getSrc2());
  EXPECT_FALSE(inst->hasDstRegister());
  EXPECT_TRUE(inst->isStore());
  dinst->scrap();

  dromajo_ptr->skip_rabbit(0, 41);  // c.sdsp
  dinst = dromajo_ptr->peek(0);
  EXPECT_EQ(0x000000008000217e, dinst->getPC());
  EXPECT_EQ(0x00000000800255b0, dinst->getAddr());
  inst = dinst->getInst();
  EXPECT_EQ(2, inst->getSrc1());
  EXPECT_EQ(18, inst->getSrc2());
  EXPECT_FALSE(inst->hasDstRegister());
  EXPECT_TRUE(inst->isStore());
  dinst->scrap();
}
