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
public:
  Emul_dromajo* dromajo_emul = 0;

  Emul_Dromajo_test() {
    std::ofstream file;
    file.open("emul_dromajo_test.toml");

    file << "[emul]\n";
    file << "num = \"1\"\n";
    file << "type = \"dromajo\"\n";
    file.close();

    Config configuration;
    configuration.init("emul_dromajo_test.toml");

    dromajo_emul = new Emul_dromajo(configuration);
    int argc = 2;
    char *argv[2] = { "emul_dromajo_test", "/home/mark/desesc/conf/dhrystone.riscv"};

    dromajo_emul->init_dromajo_machine(argc, argv);
    dromajo_emul->skip_rabbit(0, 805);
  }

  ~Emul_Dromajo_test() {
      delete dromajo_emul;
   }

  void TearDown() override {
    // Graph_library::sync_all();
  }

};

TEST_F(Emul_Dromajo_test, bge_test) {
  Dinst *dinst = dromajo_emul->peek(0);
  EXPECT_EQ(0x000000008000208c, dinst->getPC());
  EXPECT_EQ(0x0000000080002098, dinst->getAddr());
  const Instruction *inst = dinst->getInst();
  EXPECT_EQ(0 ,inst->getSrc1());
  EXPECT_EQ(10 ,inst->getSrc2());
  EXPECT_FALSE(inst->hasDstRegister());
  EXPECT_TRUE(inst->isBranch());
  dinst->recycle();
  //delete dromajo_emul;
}

TEST_F(Emul_Dromajo_test, addi_test) {
  dromajo_emul->skip_rabbit(0, 7);
  Dinst *dinst = dromajo_emul->peek(0);
  EXPECT_EQ(0x0000000080002afc, dinst->getPC());
  const Instruction *inst = dinst->getInst();
  EXPECT_EQ(3 ,inst->getSrc1());
  EXPECT_EQ(14, inst->getDst1());
  EXPECT_TRUE(inst->isALU());
  dinst->recycle();
  //delete dromajo_emul;
}

TEST_F(Emul_Dromajo_test, sw_test) {
  dromajo_emul->skip_rabbit(0, 13);
  Dinst *dinst = dromajo_emul->peek(0);
  EXPECT_EQ(0x0000000080002b0e, dinst->getPC());
  EXPECT_EQ(0x0000000080025E98, dinst->getAddr());
  const Instruction *inst = dinst->getInst();
  EXPECT_EQ(8 ,inst->getSrc1());
  EXPECT_EQ(25 ,inst->getSrc2());
  EXPECT_FALSE(inst->hasDstRegister());
  EXPECT_TRUE(inst->isStore());
  dinst->recycle();
  //delete dromajo_emul;
}

