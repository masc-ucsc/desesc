// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

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

    file.open("config_test_sample.toml");

    file << "[sec1]\n";
    file << "foo = \"mytxt\"\n";

    file << "[sec2]\n";
    file << "vfoo1 = [1,2,3]\n";
    file << "vfoo2 = [\"a\",\"b\"]\n";

    file << "[sec3]\n";
    file << "vfoo1 = \"PoTaTo\"\n";

    file << "[int_test]\n";
    file << "a = -33\n";
    file << "b = 33\n";

    file << "[base]\n";
    file << "a = [\"leaf1\", \"leaf2\"]\n";
    file << "[leaf1]\n";
    file << "v=3\n";
    file << "str=foo\n";
    file << "[leaf2]\n";
    file << "v=4\n";
    file << "str=bar\n";

    file.close();
  }

  void TearDown() override {
    // Graph_library::sync_all();
  }
};

TEST_F(Config_test, trivial) {
  Config::init("config_test_sample.toml");

  EXPECT_EQ(Config::get_string("sec1", "foo"), "mytxt");
  EXPECT_EQ(Config::get_array_size("sec2", "vfoo1"), 3);
  EXPECT_EQ(Config::get_array_size("sec2", "vfoo2"), 2);

  EXPECT_EQ(Config::get_string("sec3", "vfoo1", {"potato", "rice"}), "potato");
  EXPECT_EQ(Config::get_string("sec3", "vfoo1", {"rice"}), "INVALID");

  EXPECT_TRUE(Config::has_entry("sec3", "vfoo1"));
  EXPECT_FALSE(Config::has_entry("sec3", "vfoo_not"));
  EXPECT_FALSE(Config::has_entry("false_entry", "vfoo1"));
}

TEST_F(Config_test, integers) {
  Config::init("config_test_sample.toml");

  EXPECT_EQ(Config::get_integer("int_test", "a"), -33);
  EXPECT_EQ(Config::get_integer("int_test", "b", -100, 100), 33);
  EXPECT_EQ(Config::get_integer("int_test", "b", -10, 10), 0);
}

TEST_F(Config_test, indirect) {
  Config::init("config_test_sample.toml");

  EXPECT_EQ(Config::get_integer("base", "a", 0, "v"), 3);
  EXPECT_EQ(Config::get_integer("base", "a", 1, "v"), 4);
  EXPECT_EQ(Config::get_integer("base", "a", 0, "v", -10, 10), 3);
  EXPECT_EQ(Config::get_integer("base", "a", 1, "v", -1, 2), 0);

  EXPECT_EQ(Config::get_string("base", "a", 0, "str"), "foo");
  EXPECT_EQ(Config::get_string("base", "a", 1, "str"), "bar");
}
