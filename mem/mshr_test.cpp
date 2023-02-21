// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "config.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

class MSHR_test : public ::testing::Test {
protected:
  void SetUp() override {}

  void TearDown() override {
    // Graph_library::sync_all();
  }
};

TEST_F(MSHR_test, trivial) { EXPECT_EQ(true, true); }
