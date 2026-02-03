# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

DESESC is an architectural simulator for computer architecture research. Key features include out-of-order and multicore modeling, Spectre-Safe modeling, TOML-based configuration, and power/thermal modeling.

## Build Commands

```bash
# Debug build (development)
bazel build -c dbg //main:desesc

# Release build (long simulations)
bazel build -c opt //main:desesc

# Run all tests
bazel test -c dbg //...

# Run specific test
bazel test -c dbg //mem:cache_test

# Build with sanitizers
bazel build -c dbg --config=asan //main:desesc   # Address sanitizer
bazel build -c dbg --config=tsan //main:desesc   # Thread sanitizer
bazel build -c dbg --config=ubsan //main:desesc  # Undefined behavior

# Build with priority-aware port allocation (fixes priority inversion)
bazel build -c dbg --copt=-DPORT_STRICT_PRIORITY //main:desesc
bazel test -c dbg --copt=-DPORT_STRICT_PRIORITY //...

# Run desesc
./bazel-bin/main/desesc -c ./conf/desesc.toml

# Clean build (if compiler tools change)
bazel clean --expunge
```

### PORT_STRICT_PRIORITY

DESESC can be built with priority-aware resource allocation to fix priority inversion issues:

**Default behavior (without flag):**
- Resource ports allocate on first-come-first-served basis
- Faster simulation (~1-2% faster)
- May exhibit priority inversion (newer instructions can delay older ones)

**With PORT_STRICT_PRIORITY:**
- Resource ports respect instruction age (older instructions have higher priority)
- Slightly slower simulation (~1-2% overhead from priority queues)
- Ensures correct temporal ordering of instruction execution

**When to use:**
- Use default for fast exploratory simulations
- Use PORT_STRICT_PRIORITY for accurate timing analysis and published results

**Note:** Simulation results WILL differ between modes - this is expected and correct.

## Regression Testing

The `goldrun_test` verifies that refactoring or code changes do not unintentionally affect simulation results. It compares simulation output against stored golden reference files.

- **For refactoring/changes that should NOT affect performance**: Run `bazel test -c dbg //main:goldrun_test` to verify the output matches the golden results. The test should pass.

- **For intentional performance changes**: After making changes that intentionally affect simulation behavior, update the golden files by running:
  ```bash
  ./main/update_golden.sh
  ```
  Then commit the updated `conf/goldrun1_desesc.result` and `conf/goldrun1_kanata_log.result`.

## Architecture

### Core Modules
- **simu/**: Core simulation engine - processor pipeline, execution clusters, branch prediction
- **mem/**: Memory hierarchy - coherent caches (MESI), MSHR, prefetchers, memory controllers
- **emul/**: Dromajo emulator integration and instruction tracking (Dinst)
- **core/**: Utilities - Config (TOML parsing), statistics, callbacks, ports, pools
- **net/**: Network interconnect modeling (routers, crossbars)
- **main/**: Entry point and BootLoader initialization

### Key Classes
- `GProcessor`/`OoOProcessor`: Out-of-order processor implementation with ROB, RAT, LSQ
- `Cluster`: Execution units with dependency windows (ExecutingCluster, ExecutedCluster, RetiredCluster)
- `CCache`: Coherent cache with MESI protocol and Spectre-safe behavior
- `Dinst`: Dynamic instruction tracking through the pipeline
- `Config`: Static TOML configuration access (Config::get_integer(), Config::get_string(), etc.)

### Configuration
TOML-based configuration in `conf/desesc.toml`. Key sections: `[soc]` (cores/emulators), `[c0]` (processor params), `[bp0]`/`[bp1]` (branch predictors), `[dl1_cache]`/`[il1_cache]`/`[privl2]`/`[l3]` (cache hierarchy).

## Code Style

- **Naming**: snake_case for variables/functions/files, CamelCase for classes (e.g., `Cluster_manager`), UPPER_CASE for constants
- **Files**: `.hpp` headers, `.cpp` implementation, `*_test.cpp` tests, `*_bench.cpp` benchmarks
- **C++23**: Use `auto`, structured bindings, `constexpr`, `std::unique_ptr` (no raw new/delete)
- **Containers**: Use `absl::flat_hash_map`/`flat_hash_set` instead of `std::unordered_*`
- **Strings**: Use `const std::string &`, `absl::StrCat`, `fmt::print` for debugging
- **Errors**: Use `Config::add_error()` during setup phase
- **Accessors**: `get_XX()` (const ref, asserts), `ref_XX()` (pointer, nullable), `find_XX()` (invalid if missing), `setup_XX()` (creates if needed)
- **Functions**: Max 100 lines, max 5-10 local variables

## Memory Hierarchy Visualization

DESESC generates `memory-arch.dot` showing the cache hierarchy:
