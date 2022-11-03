// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "benchmark/benchmark.h"
#include "callback.hpp"

void                                          counter_fsm();
typedef StaticCallbackFunction0<&counter_fsm> counter_fsmCB;
typedef CallbackFunction0<&counter_fsm>       counter2_fsmCB;

int total = 0;

void counter_fsm() {
  static int local = 1;

  local++;

  // printf("counter_fms local:%d clock:@%lld\n", local, (long long)globalClock);

  if (total & 1)
    counter2_fsmCB::create()->schedule(1);  // +1cycle
  else
    counter2_fsmCB::create()->schedule(3);  // +3cycle

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

#ifndef NDEBUG
BENCHMARK(BM_callback)->Arg(128);
#else
BENCHMARK(BM_callback)->Arg(512);
#endif

int main(int argc, char* argv[]) {
  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
}
