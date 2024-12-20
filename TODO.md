
Main transient simulation support:

SCB or Store Completion buffer
 -Switch to non-inclusive (free-running) caches
 -dcache miss go directly to scb
 -stores and cache misses go to from SCB to cache on non-PNR

 -Performance Impact:
  +Compare against no-allocate cache misses in SCB

Everything fully-pipelined:
 -Cache line fills, ALUs... Fully pipeline to allow flush no contention
  +Possible to do without fully pipeplined in hardware but hard in simulator
  +The complication is flush in priority order inversion
 -No perf overhead, but can have frequency/area overhead

Priority order:
 -All the scheduling in a cycle strictly follows priority order (age)

Branch Predictor and Store Buffer Update
 -Update Predictors at pre_retire (PNR), not speculative
 -If updated early like branch history, perfect fix

Transient randomization
 -Transients due to branch misspredict (not ld/st) generated randomly for transient verification

==========================
TODO for Transients:

ICB or Instruction Cache Buffer:

 -icache buffer or ICB (similar to scb but for i-cache misses)
  +Update IL1 only on non-transient pre_retire - update LRU/state
  +Allocate on IL1 only on non-transient pre_retire
  +Max "oustanding misses". If number passes limit, stall fetch

 -Performance Impact:
  -Being able to compare performance icb vs update at fetch without ICB

Prefetcher:
 -Prefetcher updates tables only at retirement
 -OK to trigger prefetch with transients (priority order)
 -Able to drop prefetch at will (older prefetch force drop on newer)

