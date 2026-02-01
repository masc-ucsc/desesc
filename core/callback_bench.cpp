// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <cstdlib>
#include <vector>

#include "benchmark/benchmark.h"
#include "callback.hpp"

void                                          counter_fsm();
typedef StaticCallbackFunction0<&counter_fsm> counter_fsmCB;
typedef CallbackFunction0<&counter_fsm>       counter2_fsmCB;
void                                          record_order(int value);
typedef CallbackFunction1<int, &record_order> record_orderCB;

int              total = 0;
std::vector<int> order;
static const int order_size = 3;

void record_order(int value) { order.push_back(value); }

void counter_fsm() {
  if (total & 1) {
    counter2_fsmCB::create()->schedule(1);  // +1cycle
  } else {
    counter2_fsmCB::create()->schedule(3);  // +3cycle
  }

  total++;
}

static void BM_callback(benchmark::State& state) {
  total = 0;
  for (auto _ : state) {
    for (int j = 0; j < state.range(0); ++j) {
      counter_fsmCB cb;
      cb.schedule(1);

      for (int i = 0; i < j; ++i) {
        EventScheduler::advanceClock();
      }
      // printf("total of %d callbacks called\n",total);
    }
  }
  state.counters["speed"] = benchmark::Counter(total, benchmark::Counter::kIsRate);
}

static void run_priority_sanity() {
  order.clear();
  order.reserve(order_size);

  record_orderCB::schedule(1, 2, 2);
  record_orderCB::schedule(1, 1, 1);
  record_orderCB::schedule(1, 3, 3);

  EventScheduler::advanceClock();

#ifdef STRICT_PRIORITY
  I(order.size() == order_size);
  I(order[0] == 1);
  I(order[1] == 2);
  I(order[2] == 3);
#else
  I(order.size() == order_size);
  I(order[0] == 2);
  I(order[1] == 1);
  I(order[2] == 3);
#endif

  I(EventScheduler::empty());
  EventScheduler::reset();
}

#ifndef NDEBUG
BENCHMARK(BM_callback)->Arg(128);
#else
BENCHMARK(BM_callback)->Arg(512);
#endif

int main(int argc, char* argv[]) {
  benchmark::Initialize(&argc, argv);
  run_priority_sanity();
  benchmark::RunSpecifiedBenchmarks();
}
