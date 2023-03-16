# The Spectre-Safe Exclusive Cache in DESESC: Implementation and Tests

## Implementation
The implementation follows the proposal, which can be found [here](https://demo.hedgedoc.org/s/F_K61OTi2).

## Tests

The test file [spectre_safe_exclusive_cache_test.cpp](spectre_safe_exclusive_cache_test.cpp) is based on the existing [coherence cache test file](cache_test.cpp). There are 4 tests for exclusiveness and 3 tests for spectre-safeness. To build and run the tests:
```
bazelisk build -c dbg //mem:spectre_safe_exclusive_cache_test

./bazel-bin/mem/spectre_safe_exclusive_cache_test
```

Some of the tests are based on the eviction. To do so, multiple (according to the associativity) reads to the same set will be issued, and the last read shall evict the first read with LRU policy.

### Exclusiveness Tests
| Test Name                  | Description                                                                                    | Pass/Fail          |
|----------------------------|------------------------------------------------------------------------------------------------|--------------------|
| readL1_exclusive           | A read to L1 should not allocate in L2                                                         | :heavy_check_mark: |
| readL2_readL1_invalidation | A read to L1 should grab from L2 and invalidate L2's copy if present                           | :heavy_check_mark: |
| readL1_dispL1_victim       | A L1 eviction should be placed into L2                                                         | :heavy_check_mark: |
| readL1L2_dispL2_no_bak_inv | A L2 eviction should not send back invalidation to higher levels (in case of free-running mode) | :heavy_check_mark: |

### Spectre-Safeness

| Test Name             | Description                                                                          | Pass/Fail          |
|-----------------------|--------------------------------------------------------------------------------------|--------------------|
| sreadL1_no_alloc      | A speculative read to L1 should not allocate in L1                                   | :heavy_check_mark: |
| readL2_sreadL1_no_inv | A speculative read to L1 should not grab from L2 and invalidate L2's copy if present | :heavy_check_mark: |
| readL1_sreadL1_no_LRU | A speculative read should not update the LRU states                                  | :heavy_check_mark: |
