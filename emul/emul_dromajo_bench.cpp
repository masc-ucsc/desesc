// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "benchmark/benchmark.h"
#include "emul_dromajo.hpp"

std::shared_ptr<Emul_dromajo> dromajo_ptr;

static void BM_InstructionExecuteAndDecode(benchmark::State& state) {
  dromajo_ptr->set_time(1024*1024*1024);  // Lots of instructions to make sure that it runs
  for (auto _ : state) {
    dromajo_ptr->execute(0);
    Dinst* dinst = dromajo_ptr->peek(0);
    dinst->scrap();
  }
}
BENCHMARK(BM_InstructionExecuteAndDecode);

static void BM_InstructionExecute(benchmark::State& state) {
  dromajo_ptr->set_time(1024*1024*1024);  // Lots of instructions to make sure that it runs
  for (auto _ : state) {
    dromajo_ptr->execute(0);
  }
}
BENCHMARK(BM_InstructionExecute);

int main(int argc, char* argv[]) {
  std::ofstream file;
  file.open("emul_dromajo_test.toml");

  file << "[soc]\n";
  file << "core = \"c0\"\n";
  file << "emul = [\"drom_emu\"]\n";
  file << "\n[drom_emu]\n";
  file << "num = \"1\"\n";
  file << "type = \"dromajo\"\n";
  file << "bench =\"conf/dhrystone.riscv\"\n";
  file << "rabbit = 1e6\n";
  file << "detail = 1e6\n";
  file << "time = 2e6\n";
  file.close();

  Config::init("emul_dromajo_test.toml");

  dromajo_ptr = std::make_shared<Emul_dromajo>();
  benchmark::RunSpecifiedBenchmarks();
}
