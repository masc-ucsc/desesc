// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "port.hpp"

#include <vector>

#include "callback.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

class Port_test : public ::testing::Test {
protected:
  void SetUp() override {
    globalClock = 0;
    EventScheduler::reset();
  }

  void TearDown() override { EventScheduler::reset(); }
};

// --- Simple (non-priority) ports ---------------------------------------------

TEST_F(Port_test, unlimited_always_available) {
  auto port = PortGeneric::create("test_unlimited", 0);

  EXPECT_EQ(port->nextSlot(true), 0ULL);
  EXPECT_EQ(port->nextSlot(true), 0ULL);
  EXPECT_EQ(port->nextSlot(true), 0ULL);

  globalClock = 10;
  EXPECT_EQ(port->nextSlot(true), 10ULL);
  EXPECT_EQ(port->nextSlot(true), 10ULL);
}

TEST_F(Port_test, fully_pipe_basic) {
  auto port = PortGeneric::create("test_pipe", 1);

  EXPECT_EQ(port->nextSlot(true), 0ULL);
  EXPECT_EQ(port->nextSlot(true), 1ULL);
  EXPECT_EQ(port->nextSlot(true), 2ULL);

  globalClock = 5;

  EXPECT_EQ(port->nextSlot(true), 5ULL);
  EXPECT_EQ(port->nextSlot(true), 6ULL);
}

TEST_F(Port_test, fully_npipe_basic) {
  auto port = PortGeneric::create("test_npipe", 3);

  // First 3 allocations at cycle 0 (3 units available)
  EXPECT_EQ(port->nextSlot(true), 0ULL);
  EXPECT_EQ(port->nextSlot(true), 0ULL);
  EXPECT_EQ(port->nextSlot(true), 0ULL);

  EXPECT_EQ(port->nextSlot(true), 1ULL);
  EXPECT_EQ(port->nextSlot(true), 1ULL);

  globalClock = 10;

  EXPECT_EQ(port->nextSlot(true), 10ULL);
  EXPECT_EQ(port->nextSlot(true), 10ULL);
  EXPECT_EQ(port->nextSlot(true), 10ULL);
  EXPECT_EQ(port->nextSlot(true), 11ULL);
}

// --- Priority-managed ports --------------------------------------------------

// Helper: advance the clock one cycle through EventScheduler so priority ports
// drain their pending queues.
static void tick_one_cycle() { EventScheduler::advanceClock(); }

TEST_F(Port_test, priority_single_unit_age_order_within_cycle) {
  auto port = PortGeneric::create("test_prio", 1, /*priority_managed=*/true);

  std::vector<Time_t> order;
  auto                push_priority = [&](Time_t prio) {
    port->schedule(true, prio, false, [&order, prio](Time_t /*when*/) { order.push_back(prio); });
  };

  push_priority(100);
  push_priority(50);
  push_priority(200);
  push_priority(75);

  // Single-unit port: one grant per cycle, highest priority (lowest ID) first.
  tick_one_cycle();
  ASSERT_EQ(order.size(), 1U);
  EXPECT_EQ(order[0], 50U);

  tick_one_cycle();
  ASSERT_EQ(order.size(), 2U);
  EXPECT_EQ(order[1], 75U);

  tick_one_cycle();
  ASSERT_EQ(order.size(), 3U);
  EXPECT_EQ(order[2], 100U);

  tick_one_cycle();
  ASSERT_EQ(order.size(), 4U);
  EXPECT_EQ(order[3], 200U);
}

TEST_F(Port_test, priority_grants_happen_at_current_cycle) {
  auto port = PortGeneric::create("test_prio_cap", 1, /*priority_managed=*/true);

  std::vector<std::pair<Time_t, Time_t>> allocations;  // (priority, when)
  auto                                   push_priority = [&](Time_t prio) {
    port->schedule(true, prio, false, [&allocations, prio](Time_t when) { allocations.push_back({prio, when}); });
  };

  push_priority(100);
  push_priority(50);
  push_priority(75);

  // Each cycle grants 1 request.  `when` equals globalClock at time of grant.
  tick_one_cycle();  // globalClock=1
  tick_one_cycle();  // globalClock=2
  tick_one_cycle();  // globalClock=3

  ASSERT_EQ(allocations.size(), 3U);
  EXPECT_EQ(allocations[0].first, 50U);
  EXPECT_EQ(allocations[0].second, 1U);
  EXPECT_EQ(allocations[1].first, 75U);
  EXPECT_EQ(allocations[1].second, 2U);
  EXPECT_EQ(allocations[2].first, 100U);
  EXPECT_EQ(allocations[2].second, 3U);
}

TEST_F(Port_test, priority_multi_unit_grants_per_cycle) {
  auto port = PortGeneric::create("test_prio_n", 2, /*priority_managed=*/true);

  std::vector<std::pair<Time_t, Time_t>> allocations;
  auto                                   push_priority = [&](Time_t prio) {
    port->schedule(true, prio, false, [&allocations, prio](Time_t when) { allocations.push_back({prio, when}); });
  };

  push_priority(100);
  push_priority(50);
  push_priority(75);
  push_priority(200);
  push_priority(150);

  tick_one_cycle();  // grants 50, 75 at cycle 1
  tick_one_cycle();  // grants 100, 150 at cycle 2
  tick_one_cycle();  // grants 200 at cycle 3

  ASSERT_EQ(allocations.size(), 5U);
  EXPECT_EQ(allocations[0].first, 50U);
  EXPECT_EQ(allocations[0].second, 1U);
  EXPECT_EQ(allocations[1].first, 75U);
  EXPECT_EQ(allocations[1].second, 1U);
  EXPECT_EQ(allocations[2].first, 100U);
  EXPECT_EQ(allocations[2].second, 2U);
  EXPECT_EQ(allocations[3].first, 150U);
  EXPECT_EQ(allocations[4].first, 200U);
  EXPECT_EQ(allocations[4].second, 3U);
}

TEST_F(Port_test, priority_late_arriving_high_priority_wins_over_waiters) {
  // Scenario from design discussion:
  //   cycle 1: T0 (prio 0), N1 (prio 1), N2 (prio 2) arrive.
  //   cycle 2: T3 (prio -1, i.e. older than N1/N2 by a speculative-ID quirk) arrives.
  // With fair drain, T3 takes cycle 2, bumping N1 to 3 and N2 to 4.
  auto port = PortGeneric::create("test_prio_fair", 1, /*priority_managed=*/true);

  std::vector<std::pair<Time_t, Time_t>> allocations;
  auto                                   push = [&](Time_t prio) {
    port->schedule(true, prio, false, [&allocations, prio](Time_t when) { allocations.push_back({prio, when}); });
  };

  push(10);  // T0
  push(20);  // N1
  push(30);  // N2

  tick_one_cycle();  // cycle 1 drain: grants T0=10. N1/N2 wait.

  push(15);  // T3 arrives at cycle 2, older than N1/N2 (15 < 20 < 30).

  tick_one_cycle();  // cycle 2: T3(15) wins over N1(20) and N2(30).
  tick_one_cycle();  // cycle 3: N1(20).
  tick_one_cycle();  // cycle 4: N2(30).

  ASSERT_EQ(allocations.size(), 4U);
  EXPECT_EQ(allocations[0].first, 10U);
  EXPECT_EQ(allocations[0].second, 1U);
  EXPECT_EQ(allocations[1].first, 15U);
  EXPECT_EQ(allocations[1].second, 2U);
  EXPECT_EQ(allocations[2].first, 20U);
  EXPECT_EQ(allocations[2].second, 3U);
  EXPECT_EQ(allocations[3].first, 30U);
  EXPECT_EQ(allocations[3].second, 4U);
}

TEST_F(Port_test, priority_unlimited_all_at_current_cycle) {
  auto port = PortGeneric::create("test_prio_unl", 0, /*priority_managed=*/true);

  std::vector<std::pair<Time_t, Time_t>> allocations;
  auto                                   push_priority = [&](Time_t prio) {
    port->schedule(true, prio, false, [&allocations, prio](Time_t when) { allocations.push_back({prio, when}); });
  };

  push_priority(100);
  push_priority(50);
  push_priority(200);

  tick_one_cycle();

  ASSERT_EQ(allocations.size(), 3U);
  // Age order:
  EXPECT_EQ(allocations[0].first, 50U);
  EXPECT_EQ(allocations[1].first, 100U);
  EXPECT_EQ(allocations[2].first, 200U);
  // Unlimited port: all fire at current cycle (1 after the advance).
  EXPECT_EQ(allocations[0].second, 1U);
  EXPECT_EQ(allocations[1].second, 1U);
  EXPECT_EQ(allocations[2].second, 1U);
}

TEST_F(Port_test, flush_transient_purges_pending_queue) {
  auto port = PortGeneric::create("test_prio_flush_q", 1, /*priority_managed=*/true);

  std::vector<Time_t> fired;
  auto                push = [&](Time_t prio, bool transient) {
    port->schedule(true, prio, transient, [&fired, prio](Time_t /*when*/) { fired.push_back(prio); });
  };

  push(50, false);
  push(75, true);
  push(100, false);
  push(150, true);

  // Flush before any drain.  Only non-transient entries survive.
  port->flush_transient();

  tick_one_cycle();  // grants 50
  tick_one_cycle();  // grants 100

  ASSERT_EQ(fired.size(), 2U);
  EXPECT_EQ(fired[0], 50U);
  EXPECT_EQ(fired[1], 100U);
}

TEST_F(Port_test, flush_transient_frees_queued_transients_for_new_reals) {
  auto port = PortGeneric::create("test_prio_flush_occ", 1, /*priority_managed=*/true);

  std::vector<std::pair<Time_t, Time_t>> allocations;
  auto                                   push = [&](Time_t prio, bool transient) {
    port->schedule(true, prio, transient, [&allocations, prio](Time_t when) { allocations.push_back({prio, when}); });
  };

  // Cycle 0: 50 (real), 75 (transient), 100 (transient), 150 (real) submitted.
  push(50, false);
  push(75, true);
  push(100, true);
  push(150, false);

  tick_one_cycle();  // cycle 1: grants 50 (oldest). 75, 100, 150 still queued.

  // Squash transients before any of them are granted.
  port->flush_transient();

  tick_one_cycle();  // cycle 2: 75 and 100 gone; only 150 remains → grants 150.

  ASSERT_EQ(allocations.size(), 2U);
  EXPECT_EQ(allocations[0].first, 50U);
  EXPECT_EQ(allocations[0].second, 1U);
  EXPECT_EQ(allocations[1].first, 150U);
  EXPECT_EQ(allocations[1].second, 2U);
}

TEST_F(Port_test, flush_transient_noop_on_simple_port) {
  auto port = PortGeneric::create("test_simple", 1);
  // Simple port: flush_transient is a no-op.  nextSlot state unaffected.
  EXPECT_EQ(port->nextSlot(true), 0ULL);
  port->flush_transient();
  EXPECT_EQ(port->nextSlot(true), 1ULL);
}
