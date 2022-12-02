// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "emul_dromajo.hpp"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <fstream>
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

//#define REGRESS_COSIM 1
#ifdef REGRESS_COSIM
#include "dromajo_cosim.h"
#endif

#include "gmock/gmock.h"
#include "gtest/gtest.h"

class Emul_Dromajo_test : public ::testing::Test {
protected:
  void SetUp() override {
    std::ofstream file;
    file.open("emul_dromajo_test.toml");

    file << "[soc]\n";
    file << "core = \"c0\"\n";
    file << "emul = \"drom_emu\"\n";
    file << "\n[drom_emu]\n";
    file << "num = \"1\"\n";
    file << "type = \"dromajo\"\n";
    file << "load = \"ck1\"\n";
    file << "rabbit = 1e6\n";
    file << "detail = 1e6\n";
    file << "time = 2e6\n";
    file.close();

    Config configuration;
    configuration.init("emul_dromajo_test.toml");

    char benchmark_name[] = "dhrystone.riscv";

    Emul_dromajo dromajo_emul(configuration);
    dromajo_emul.init_dromajo_machine(benchmark_name);
    dromajo_ptr = &dromajo_emul;
  }

  Emul_dromajo* dromajo_ptr;

  void TearDown() override {
    // Graph_library::sync_all();
  }

};

TEST_F(Emul_Dromajo_test, dhrystone_test) {
  Emul_dromajo dromajo_emul = *dromajo_ptr;
  EXPECT_FALSE(dromajo_ptr == NULL);

  dromajo_emul.skip_rabbit(0, 560);
  Dinst *dinst = dromajo_emul.peek(0); //csrr
  EXPECT_EQ(0x0000000080002aaa, dinst->getPC());
  const Instruction *inst = dinst->getInst();
  EXPECT_EQ(15, inst->getDst1());
  dinst->recycle();

  dromajo_emul.skip_rabbit(0, 197);
  dinst = dromajo_emul.peek(0); //bge
  EXPECT_EQ(0x0000000080002094, dinst->getPC());
  EXPECT_EQ(0x00000000800020a4, dinst->getAddr());
  inst = dinst->getInst();
  EXPECT_EQ(0 ,inst->getSrc1());
  EXPECT_EQ(10 ,inst->getSrc2());
  EXPECT_FALSE(inst->hasDstRegister());
  EXPECT_TRUE(inst->isBranch());
  dinst->recycle();

  dromajo_emul.skip_rabbit(0, 6);  //ret
  dinst = dromajo_emul.peek(0);
  EXPECT_EQ(0x00000000800020ae, dinst->getPC());
  inst = dinst->getInst();
  EXPECT_TRUE(inst->isFuncRet());
  dinst->recycle();

  dromajo_emul.skip_rabbit(0, 2);  //addi
  dinst = dromajo_emul.peek(0);
  EXPECT_EQ(0x0000000080002b38, dinst->getPC());
  inst = dinst->getInst();
  EXPECT_EQ(8 ,inst->getSrc1());
  EXPECT_EQ(12, inst->getDst1());
  EXPECT_TRUE(inst->isALU());
  dinst->recycle();

  dromajo_emul.skip_rabbit(0, 5);  //sw
  dinst = dromajo_emul.peek(0);
  EXPECT_EQ(0x0000000080002b46, dinst->getPC());
  EXPECT_EQ(0x0000000080025658, dinst->getAddr());
  inst = dinst->getInst();
  EXPECT_EQ(8 ,inst->getSrc1());
  EXPECT_EQ(15 ,inst->getSrc2());
  EXPECT_FALSE(inst->hasDstRegister());
  EXPECT_TRUE(inst->isStore());
  dinst->recycle();
}
