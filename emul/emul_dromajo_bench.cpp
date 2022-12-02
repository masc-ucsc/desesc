// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "benchmark/benchmark.h"
#include "emul_dromajo.hpp"

Emul_dromajo* dromajo_ptr;

static void BM_InstructionExecuteAndDecode(benchmark::State& state) {
  for (auto _ : state) {
    dromajo_ptr->skip_rabbit(0, 1);
    Dinst* dinst = dromajo_ptr->peek(0);
    dinst->recycle();
  }
}
BENCHMARK(BM_InstructionExecuteAndDecode);

static void BM_InstructionExecute(benchmark::State& state) {
  for (auto _ : state) {
    dromajo_ptr->skip_rabbit(0, 1);
  }
}
BENCHMARK(BM_InstructionExecute);

int main(int argc, char* argv[]) {
  std::ofstream file;
  file.open("emul_dromajo_test.toml");

  file << "[soc]\n";
  file << "core = \"c0\"\n";
  file << "emul = \"drom_emu\"\n";
  file << "\n[drom_emu]\n";
  file << "num = \"1\"\n";
  file << "type = \"dromajo\"\n";
  file.close();

  Config configuration;
  configuration.init("emul_dromajo_test.toml");

  char benchmark_name[] = "dhrystone.riscv";

  Emul_dromajo dromajo_emul(configuration);
  dromajo_emul.init_dromajo_machine(benchmark_name);
  dromajo_ptr = &dromajo_emul;
  benchmark::RunSpecifiedBenchmarks();
}
