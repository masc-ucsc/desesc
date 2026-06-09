# Debugging DESESC

Quick reference for debugging simulation hangs, deadlocks, and state-corruption
bugs. Generic steps — adapt the PCs, IDs, and clocks to whatever run you are
looking at.

## Reproduce

Keep two configs side-by-side so you can flip transients on/off without editing
the same file:

```bash
# Transients off (usually the known-good path)
./bazel-bin/main/desesc -c ./conf/desesc.toml > pp.notran

# Transients on (usually where bugs live)
./bazel-bin/main/desesc -c ./desesc_tran.toml > pp.tran
```

- If a run hangs, wrap it in `timeout 20s` so you still get flushed output to
  grep.
- Prefer a debug build (`bazel build -c dbg //main:desesc`) first: asserts fire
  instead of silently corrupting state.

## Trace-based workflow

`oooprocessor.cpp` has `#define ESESC_TRACE` enabled. Every committed
instruction produces:

```
TR <ID> <PC> <dst1,dst2>= <src1> op=<opcode> <src2>  ft:<> rt:<> it:<> et:<> @<globalClock>
```

`ft/rt/it/et` are relative times from fetch/rename/issue/execute. `@clk` is the
absolute retire clock.

### Where did the sim get stuck?

```bash
grep "TR " pp.tran | tail -10
```

If the tail shows retires up to some clock `T`, and the sim ran many seconds
past `T` without more `TR` lines, retire is stalled. The last retired ID and PC
are your anchor for everything below.

### Compare transient vs non-transient by PC

IDs differ between runs because transients consume `Dinst` IDs. Match by PC:

```bash
grep "TR " pp.notran | grep -C2 "<pc> <regs> op=<opcode>"
grep "TR " pp.tran   | grep -C2 "<pc> <regs> op=<opcode>"
```

If the non-transient run retires the fall-through PC right after, but the
transient run stops cleanly at a branch with nothing following, the bug lives
on the misspeculation / flush path, not in the instruction itself.

## Stall diagnosis (ROB / rROB)

`OoOProcessor::retire()` already dumps ROB/rROB sizes. Typical deadlock
signatures:

- ROB stops shrinking, `rROB` drains and stays empty (or vice versa).
- New IDs keep getting `add_inst`'d; the gap between the last retired ID and
  the newest added ID is roughly the transients consumed.

Once you know the ROB is stuck at the head, you want to know *which* dinst and
*why* `preretire()` returned false. Drop a throttled print at the `break` in
`retire()`:

```cpp
if (!done) {
  static uint64_t stuck_ctr = 0;
  if ((stuck_ctr++ & 0xFFF) == 0) {
    fmt::print("STUCK top id={} pc={:x} op={} T={} Rn={} I={} Exg={} Ex={} Disp={} FT={} TFT={} hasDeps={} clk={}\n",
               dinst->getID(), dinst->getPC(), dinst->getInst()->getOpcode(),
               dinst->isTransient(), dinst->isRenamed(), dinst->isIssued(),
               dinst->isExecuting(), dinst->isExecuted(), dinst->isDispatched(),
               dinst->is_flush_transient(), dinst->is_try_flush_transient(),
               dinst->hasDeps(), globalClock);
  }
  break;
}
```

Gate by clock once you know the stall window (`if (globalClock > T && ...)`)
to keep the log small.

The state tuple tells you which `preretire()` gate failed:

| Resource   | `preretire` gate        |
|------------|-------------------------|
| FURALU     | `isExecuted()`          |
| FUGeneric  | `isExecuted()`          |
| FUBranch   | `isExecuted()`          |
| FULoad     | `isDispatched()`        |
| FUStore    | `isExecuted()`          |

`Rn=true I=true Exg=false Ex=false` on an ALU means it was issued onto a port
but the port callback never fired — usually a port queue was flushed out from
under it.

## `advanceClock` and the port fixed-point drain

`EventScheduler::advanceClock` runs cbQ callbacks and drains priority-managed
port queues to a fixed point. Two places where events can be lost vs. not:

- `flush_transient_ports()` in `core/port.cpp` filters `transient` entries out
  of `PortPipePriority::queue` / `PortUnlimitedPriority::queue`. Scheduled
  callbacks for those entries are gone.
- The regular cbQ (`Resource::executingCB`, `executedCB`, …) is **not**
  flushable. Once scheduled, the callback *will* fire at its `scheduleAbs`
  time.

This asymmetry is the source of the use-after-recycle hazard: the port callback
for a transient is flushed, but its downstream cbQ callbacks are already
queued. Those fire on a dinst that retire may have already destroyed. After
`dInstPool` recycles the slot, the stale callback hits a fresh `Dinst` — it
looks like `Rn=0 I=0` but suddenly has `Ex=<now>` because `markExecuted()` just
ran on it. Classic symptom: `I(RAT1Entry)` in `Dinst::clearRATEntry()` on an
unexpected ID.

When you suspect this, print inside `clearRATEntry()` on the null path:

```cpp
if (!RAT1Entry) {
  fmt::print("clearRATEntry null id={} pc={:x} op={} T={} Rn={} I={} Ex={} Perf={}\n",
             ID, pc, (int)inst.getOpcode(),
             (int)transient, (int)renamed, (int)issued,
             (int)executed, (int)performed);
}
```

`Rn=0 I=0 Ex=<large time>` is always a recycled-slot victim.

## Transient lifecycle cheat-sheet

When `do_random_transients = true`, speculative ALU ops are injected on a
branch miss:

1. `add_inst_transient_on_branch_miss()` creates transients with
   `setTransient()` + `set_spec()`, enqueues them in the IF bucket.
2. `OoOProcessor::add_inst()` uses **TRAT** (not RAT) for transient→transient
   dependencies. Transients never depend on real instructions and vice versa.
3. Normal rename → DepWindow → port schedule → execute → `markExecuted`.
4. On resolution, `flush_transient_inst_on_fetch_ready()` cleans up:
   - inst queue, pipeline buffer, received bucket
   - ROB: `flush_transient_from_rob()` destroys executed transients and marks
     in-flight ones with `mark_flush_transient` / `mark_try_flush_transient`.
   - SCB: `flush_transient_from_scb()`
   - Ports: **intentionally not flushed** (see the
     `// Do NOT flush_transient_ports()` comment in `simu/gprocessor.cpp`).
     Flushing leaves dangling cbQ callbacks; letting ports fire lets the
     transient reach `isExecuted` so retire destroys it cleanly via the
     `is_flush_transient` path.
5. In `retire()`, transients that reach ROB.top take the `is_flush_transient`
   → destroy branch (no register/LSQ double-free: those resources were
   already released during the flush).

Invariant when transient bugs appear: **TRAT and RAT do not cross-pollute**.
`OoOProcessor::add_inst()` selects based on `dinst->isTransient()`. If a
non-transient dinst points at TRAT (or vice versa), a flush step corrupted
state.

## Assertions worth knowing

- `dinst.cpp` `I(RAT1Entry)` — called on a dinst that was never renamed or
  whose slot was recycled. See the recycled-slot discussion above.
- `dinst.cpp` `I(issued); I(executed)` in `Dinst::destroy()` — retire trying
  to destroy something that never fully executed. Usually a pre-retire gate
  returned true incorrectly; check which resource.
- `FastQueue::pop_from_back` does **not** guard `nElems--`. Calling it when
  empty wraps `nElems` to `UINT32_MAX`; the `TRACK_TIMELEAK` loop in retire
  then iterates billions of times. Symptom: "sim is alive but makes no
  progress." Count ROB pops vs. pushes.

## Useful greps

```bash
# Last retirement clock / ID
grep "TR " pp.tran | tail -5

# Did fetch get unblocked on branch-miss recovery?
grep "unBlockFetch" pp.tran | tail

# Loads stuck in the LSQ?
grep -c "Load_add_inst for  dinstID" pp.tran
grep "OutsLoadsStall" pp.tran | tail

# Lock detector (OoOProcessor::retire_lock_check) fires
grep "Lock detected" pp.tran

# Transient destructions at retire time
grep "Transient poping from rob" pp.tran | tail
```

## Rules that have bitten me

- Don't "fix" a deadlock by destroying a transient before its cbQ callbacks
  fire. The dinst pool will recycle the slot and pending callbacks will
  corrupt the next Dinst that lands there. Either flush the callbacks too, or
  wait for them.
- `flush_transient_ports()` and `flush_transient_from_rob()` must agree on
  what "flushed" means. If one destroys/pops and the other leaves a pending
  reference, you see either the deadlock or the recycled-slot assertion.
- Transient state is monotonic (`setTransient` only sets true). Don't try to
  "un-transient" an instruction; flush it and re-fetch.
- `getExecutedTime() + RetireDelay >= globalClock` in the rROB loop silently
  breaks out. If retires stop at a specific head and you don't see a
  `cluster->retire` failure, check this condition first.
