
#include "benchmark/benchmark.h"
#include "cachecore.hpp"
#include "config.hpp"
#include "report.hpp"

class SampleState : public StateGeneric<long> {
public:
  int32_t id;

  SampleState(int32_t lineSize) { id = 0; }
  bool operator==(SampleState s) const { return id == s.id; }
};

using MyCacheType = CacheGeneric<SampleState, long>;

MyCacheType* cache;

timeval stTime;
timeval endTime;
double  nAccess;
double  nMisses;

void startBench() {
  nAccess = 0;
  nMisses = 0;
  gettimeofday(&stTime, 0);
}

void endBench(const char* str) {
  gettimeofday(&endTime, 0);

  double usecs = (endTime.tv_sec - stTime.tv_sec) * 1000000 + (endTime.tv_usec - stTime.tv_usec);

  fmt::print("{}: {} Maccesses/s {}%\n", str, nAccess / usecs, 100 * nMisses / nAccess);
}

#define MSIZE 256

double A[MSIZE][MSIZE];
double B[MSIZE][MSIZE];
double C[MSIZE][MSIZE];

void benchMatrix(const char* str) {
  startBench();

  MyCacheType::CacheLine* line;

  for (int32_t i = 0; i < MSIZE; i++) {
    for (int32_t j = 0; j < MSIZE; j++) {
      // A[i][j]=0;

      // A[i][j]=...
      line = cache->writeLine((long)&A[i][j]);
      nAccess++;
      if (line == 0) {
        cache->fillLine((long)&A[i][j]);
        nAccess++;
        nMisses++;
      }

      for (int32_t k = 0; k < MSIZE; k++) {
        // A[i][j] += B[i][j]*C[j][k];

        // = ... A[i][j]
        line = cache->readLine((long)&A[i][j]);
        nAccess++;
        if (line == 0) {
          cache->fillLine((long)&A[i][j]);
          nAccess++;
          nMisses++;
        }

        // = ... B[i][j]
        line = cache->readLine((long)&B[i][j]);
        nAccess++;
        if (line == 0) {
          cache->fillLine((long)&B[i][j]);
          nAccess++;
          nMisses++;
        }

        // = ... C[i][j]
        line = cache->readLine((long)&C[i][j]);
        nAccess++;
        if (line == 0) {
          cache->fillLine((long)&C[i][j]);
          nAccess++;
          nMisses++;
        }

        // A[i][j]=...;
        line = cache->writeLine((long)&A[i][j]);
        nAccess++;
        if (line == 0) {
          cache->fillLine((long)&A[i][j]);
          nAccess++;
          nMisses++;
        }
      }
    }
  }

  endBench(str);
}

static void setup_config() {
  std::ofstream file;

  file.open("cachecore.toml");

  file << "[dl1_cache]\n";
  file << "type       = \"cache\"   # or nice\n";
  file << "size       = 32768\n";
  file << "line_size  = 64\n";
  file << "delay      = 2         # hit delay\n";
  file << "repl_policy = \"LRU\"\n";
  file << "miss_delay = 8\n";
  file << "assoc      = 4\n";
  file << "\n";

  file.close();
}

static void BM_cachecore(benchmark::State& state) {
  Report::init();
  Config::init("cachecore.toml");

  cache = MyCacheType::create("dl1_cache", "", "tst1");

  int32_t assoc = Config::get_power2("dl1_core", "assoc");
  for (int32_t i = 0; i < assoc; i++) {
    uint64_t addr = (i << 8) + 0xfa;

    MyCacheType::CacheLine* line = cache->findLine(addr);
    if (line) {
      fmt::print("ERROR: Line {:x} ({:x}) found\n", cache->calcAddr4Tag(line->getTag()), addr);
      exit(-1);
    }
    line     = cache->fillLine(addr);
    line->id = i;
  }

  for (int32_t i = 0; i < assoc; i++) {
    uint64_t addr = (i << 8) + 0xFa;

    MyCacheType::CacheLine* line = cache->findLine(addr);
    if (line == 0) {
      fmt::print("ERROR: Line ({:x}) NOT found\n", addr);
      exit(-1);
    }
    if (line->id != i) {
      fmt::print("ERROR: Line {:x} ({:x}) line->id {} vs id {} (bad LRU policy)\n",
                 cache->calcAddr4Tag(line->getTag()),
                 addr,
                 line->id,
                 i);
      exit(-1);
    }
  }

  cache = MyCacheType::create("dl1_cache", "", "L1");
  for (auto _ : state) {
    for (int j = 0; j < state.range(0); ++j) {
      benchMatrix("dl1_core");
    }
  }

  state.counters["speed"] = benchmark::Counter(nAccess, benchmark::Counter::kIsRate);
}

#ifndef NDEBUG
BENCHMARK(BM_cachecore)->Arg(2);
#else
BENCHMARK(BM_cachecore)->Arg(4);
#endif

int main(int argc, char* argv[]) {
  setup_config();
  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
}
