// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "port.hpp"

#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

class Port_test : public ::testing::Test {
protected:
  void SetUp() override {
    // Reset global clock
    globalClock = 0;
    EventScheduler::reset();
  }

  void TearDown() override { EventScheduler::reset(); }
};

TEST_F(Port_test, unlimited_always_available) {
  auto port = PortGeneric::create("test_unlimited", 0);

  // PortUnlimited should always return current cycle
  EXPECT_EQ(port->nextSlot(true), 0);
  EXPECT_EQ(port->nextSlot(true), 0);
  EXPECT_EQ(port->nextSlot(true), 0);

  globalClock = 10;
  EXPECT_EQ(port->nextSlot(true), 10);
  EXPECT_EQ(port->nextSlot(true), 10);
}

TEST_F(Port_test, fully_pipe_basic) {
  auto port = PortGeneric::create("test_pipe", 1);

  // First allocation at cycle 0
  EXPECT_EQ(port->nextSlot(true), 0);

  // Second allocation at cycle 1 (port busy)
  EXPECT_EQ(port->nextSlot(true), 1);

  // Third allocation at cycle 2
  EXPECT_EQ(port->nextSlot(true), 2);

  // Advance clock
  globalClock = 5;

  // Should allocate at cycle 5 (catches up)
  EXPECT_EQ(port->nextSlot(true), 5);
  EXPECT_EQ(port->nextSlot(true), 6);
}

TEST_F(Port_test, fully_npipe_basic) {
  auto port = PortGeneric::create("test_npipe", 3);

  // First 3 allocations at cycle 0 (3 units available)
  EXPECT_EQ(port->nextSlot(true), 0);
  EXPECT_EQ(port->nextSlot(true), 0);
  EXPECT_EQ(port->nextSlot(true), 0);

  // Fourth allocation at cycle 1 (all units busy)
  EXPECT_EQ(port->nextSlot(true), 1);

  // Fifth allocation at cycle 1
  EXPECT_EQ(port->nextSlot(true), 1);

  // Advance clock
  globalClock = 10;

  // Should allocate 3 at cycle 10, then roll to 11
  EXPECT_EQ(port->nextSlot(true), 10);
  EXPECT_EQ(port->nextSlot(true), 10);
  EXPECT_EQ(port->nextSlot(true), 10);
  EXPECT_EQ(port->nextSlot(true), 11);
}

#ifdef PORT_STRICT_PRIORITY

TEST_F(Port_test, priority_immediate_allocation) {
  auto port = PortGeneric::create("test_priority", 1);

  // First request should succeed immediately
  auto [when1, retry1] = port->tryNextSlot(true, 100);
  EXPECT_EQ(when1, 0);
  EXPECT_FALSE(retry1);  // No retry needed
}

TEST_F(Port_test, priority_deferred_allocation) {
  auto port = PortGeneric::create("test_priority", 1);

  // First request succeeds
  auto [when1, retry1] = port->tryNextSlot(true, 100);
  EXPECT_EQ(when1, 0);
  EXPECT_FALSE(retry1);

  // Second request should need retry (port busy)
  auto [when2, retry2] = port->tryNextSlot(true, 101);
  EXPECT_EQ(when2, 1);  // Would be allocated at cycle 1
  EXPECT_TRUE(retry2);  // Retry needed
}

TEST_F(Port_test, priority_ordering_single_unit) {
  auto port = PortGeneric::create("test_priority", 1);

  std::vector<Time_t> execution_order;

  // First request allocates immediately
  auto [when1, retry1] = port->tryNextSlot(true, 100);
  EXPECT_FALSE(retry1);
  execution_order.push_back(100);

  // Queue requests with different priorities (lower ID = higher priority)
  // Request from instruction 50 (higher priority)
  auto [when2, retry2] = port->tryNextSlot(true, 50);
  EXPECT_TRUE(retry2);
  port->queueRequest(true, 50, [&execution_order](Time_t when) {
    (void)when;
    execution_order.push_back(50);
  });

  // Request from instruction 200 (lower priority)
  auto [when3, retry3] = port->tryNextSlot(true, 200);
  EXPECT_TRUE(retry3);
  port->queueRequest(true, 200, [&execution_order](Time_t when) {
    (void)when;
    execution_order.push_back(200);
  });

  // Request from instruction 75 (medium priority)
  auto [when4, retry4] = port->tryNextSlot(true, 75);
  EXPECT_TRUE(retry4);
  port->queueRequest(true, 75, [&execution_order](Time_t when) {
    (void)when;
    execution_order.push_back(75);
  });

  // Advance clock and process pending requests (one per cycle for single-unit port)
  for (int cycle = 1; cycle <= 3; cycle++) {
    globalClock = cycle;
    EventScheduler::advanceClock();
  }

  // Verify priority ordering: 50 < 75 < 200 (lower ID first)
  ASSERT_EQ(execution_order.size(), 4);
  EXPECT_EQ(execution_order[0], 100);  // Immediate at cycle 0
  EXPECT_EQ(execution_order[1], 50);   // Highest priority at cycle 1
  EXPECT_EQ(execution_order[2], 75);   // Medium priority at cycle 2
  EXPECT_EQ(execution_order[3], 200);  // Lowest priority at cycle 3
}

TEST_F(Port_test, priority_ordering_multi_unit) {
  auto port = PortGeneric::create("test_priority", 2);  // 2 units

  std::vector<Time_t> execution_order;

  // First 2 requests allocate immediately
  auto [when1, retry1] = port->tryNextSlot(true, 100);
  EXPECT_FALSE(retry1);
  execution_order.push_back(100);

  auto [when2, retry2] = port->tryNextSlot(true, 101);
  EXPECT_FALSE(retry2);
  execution_order.push_back(101);

  // Queue 3 more requests (port full)
  port->queueRequest(true, 50, [&execution_order](Time_t when) {
    (void)when;
    execution_order.push_back(50);
  });

  port->queueRequest(true, 150, [&execution_order](Time_t when) {
    (void)when;
    execution_order.push_back(150);
  });

  port->queueRequest(true, 75, [&execution_order](Time_t when) {
    (void)when;
    execution_order.push_back(75);
  });

  // Advance clock and process (2 units available)
  globalClock = 1;
  EventScheduler::advanceClock();

  // Should process 2 highest priority requests (50, 75)
  ASSERT_GE(execution_order.size(), 4);
  EXPECT_EQ(execution_order[0], 100);  // Immediate
  EXPECT_EQ(execution_order[1], 101);  // Immediate
  EXPECT_EQ(execution_order[2], 50);   // Highest priority
  EXPECT_EQ(execution_order[3], 75);   // Second priority

  // Advance again to process remaining request
  globalClock = 2;
  EventScheduler::advanceClock();

  ASSERT_EQ(execution_order.size(), 5);
  EXPECT_EQ(execution_order[4], 150);  // Lowest priority
}

TEST_F(Port_test, priority_multi_cycle_processing) {
  auto port = PortGeneric::create("test_priority", 1);

  std::vector<std::pair<Time_t, Time_t>> allocations;  // (priority, when)

  // Queue 5 requests in cycle 0
  for (Time_t prio : {100, 50, 200, 25, 150}) {
    auto [when, retry] = port->tryNextSlot(true, prio);
    if (!retry) {
      allocations.push_back({prio, when});
    } else {
      port->queueRequest(true, prio, [&allocations, prio](Time_t when) { allocations.push_back({prio, when}); });
    }
  }

  // Process cycles (need 5 cycles for 5 requests on single-unit port)
  for (int cycle = 1; cycle <= 5; cycle++) {
    globalClock = cycle;
    EventScheduler::advanceClock();
  }

  // Verify all requests processed
  ASSERT_EQ(allocations.size(), 5);

  // Verify priority ordering (lower ID = higher priority = earlier allocation)
  // Request order: 100 (immediate), then queued: 50, 200, 25, 150
  // Processing order should be: 100 (immediate), 25, 50, 150, 200 (by priority)
  EXPECT_EQ(allocations[0].first, 100);  // Immediate at cycle 0
  EXPECT_EQ(allocations[1].first, 25);   // Highest priority (cycle 1)
  EXPECT_EQ(allocations[2].first, 50);   // Second priority (cycle 2)
  EXPECT_EQ(allocations[3].first, 150);  // Third priority (cycle 3)
  EXPECT_EQ(allocations[4].first, 200);  // Lowest priority (cycle 4)
}

#else

TEST_F(Port_test, fifo_ordering_without_priority) {
  auto port = PortGeneric::create("test_fifo", 1);

  std::vector<Time_t> allocation_times;

  // Allocate in order: 100, 50, 200
  // Without priority, should get times in FIFO order: 0, 1, 2
  allocation_times.push_back(port->nextSlot(true));  // 100
  allocation_times.push_back(port->nextSlot(true));  // 50
  allocation_times.push_back(port->nextSlot(true));  // 200

  // FIFO order (call order, not priority)
  EXPECT_EQ(allocation_times[0], 0);
  EXPECT_EQ(allocation_times[1], 1);
  EXPECT_EQ(allocation_times[2], 2);
}

#endif
