
#include "config.hpp"

#include <random>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

class Config_test : public ::testing::Test {
protected:
  void SetUp() override {

    std::ofstream file;

    file.open ("config_test_sample.toml");

    file << "[sec1]\n";
    file << "foo = \"mytxt\"\n";
    file << "[sec2]\n";
    file << "vfoo1 = [1,2,3]\n";
    file << "vfoo2 = [\"a\",\"b\"]\n";

    file.close();
  }

  void TearDown() override {
    // Graph_library::sync_all();
  }
};

TEST_F(Config_test, trivial) {

  Config::init("config_test_sample.toml");

  EXPECT_EQ(Config::get_string("sec1","foo"), "mytxt");
  EXPECT_EQ(Config::get_array_size("sec2","vfoo1"), 3);
  EXPECT_EQ(Config::get_array_size("sec2","vfoo2"), 2);
}

