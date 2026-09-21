# Communication patterns on HammerBlade — evidence and open questions

Raw material for a compiler/hardware co-design paper on dependency-driven
execution. Everything here is measured on a 16x8 bigblade pod in RTL unless
stated. Source kernels are in-tree; every number is reproducible from a
`make profile.log` in the named test directory.

---

## 1. The problem is real, and it is synchronization

The proposal's premise is that manual management of communication and
synchronization is the barrier. Two measurements make that concrete.

**Synchronization dominates a hand-written systolic kernel.** `mm/sysreg` at
128^3 on a full pod:

| | cycles | share |
| --- | --- | --- |
| `stall_lr_aq` (waiting on a producer) | 7,891,982 | **52.6%** |
| memory stall | 776,000 | 5.2% |
| instructions retired | 6,114,071 | 40.8% |

More than half of all core-cycles are spent waiting on a hand-rolled
full/empty handshake. This is the number that motivates hardware support.

**And the machine is not bandwidth-limited.** At 256^3 on a full pod, DRAM
utilization is **16.19%** while `stall_remote_req` -- cores waiting to *issue*
a request -- is **22.25%**. There is roughly 6x bandwidth headroom the cores
cannot reach. The limiting resource is request issue and synchronization, not
memory.

---

## 2. The central tradeoff, quantified exactly

The cleanest result of the study. Two kernels, identical inner loops (same
register tile, same fused FMA), same shape, same 128 tiles. **Only the
dataflow differs.**

| 128^3, full pod | data-parallel | systolic |
| --- | --- | --- |
| DRAM loads | 524,258 | **32,768** (16x fewer) |
| memory stall | **51.5%** | 5.2% |
| handshake stall | 6.6% | **52.6%** |
| **runtime** | **117,416** | **118,367** |

The systolic array did exactly what it promised -- 16x less DRAM traffic,
memory stall down 10x -- and gave all of it back in synchronization. **0.8%
apart.**

**Interpretation for the paper.** Data-parallel tiles stall on memory
*concurrently*; a systolic chain converts those stalls into a *serial*
dependency, because one edge tile staging from DRAM stalls everyone
downstream. Redundant traffic is wasteful but parallel. Optimal traffic is
serial. At equal capacity they cost the same.

This is precisely the gap a compiler + hardware channel should close: keep the
traffic optimality, remove the serialization.

---

## 3. Traffic locality matters more than traffic volume

| same shape, same tile count | `stall_remote_req` |
| --- | --- |
| data-parallel (long-haul to column caches) | **22.25%** |
| systolic (nearest-neighbour) | **1.81%** |

A 12x difference in injection stall for traffic of comparable volume. Distance
and destination class, not byte count, determine contention. Any cost model
that counts only bytes moved will mispredict on this machine.

---

## 4. Software synchronization has an irreducible cost — measured

What one hand-written message costs today, from `mm/systolic` at 16^3 / 4x2:

- 1024 payload stores
- 320 flag stores (full + credit)
- ~450 `lr` / `lr_aq` waits
- ...to save 1024 remote loads.

A bad trade. The per-message overhead is what does not amortize.

**And batching cannot fix it in software.** `mm/sysreg` batches KC k-steps per
handshake. Sweeping KC at a fixed shape:

| KC | handshakes | instructions | runtime |
| --- | --- | --- | --- |
| 4 | 40 | 17,156 | **4,995** |
| 16 | 10 | 13,685 | 5,810 |

KC=16 cut instructions 20% (the handshake amortized exactly as designed) and
ran **16% slower**, because `stall_lr_aq` nearly doubled: a bigger window
makes a tile wait for the whole window before it can compute anything.

**This is the strongest argument in the study for hardware.** In software,
synchronization granularity and pipeline granularity are the same knob, and
they pull in opposite directions. Hardware completion tracking decouples them:
transfer a large block for efficiency while releasing consumers incrementally.
Nothing in software can do both.

---

## 5. The fixed scratchpad budget forces worse algorithms — three cases

4 KB per tile, ~768 usable words. We hit the wall three independent times:

1. **Buffers compete with each other.** In `mm/sysreg`, message windows and
   staging chunks share the budget, so the register tile could only amortize
   over BLK_C=4 instead of 16: **1.23 scratchpad ops per MAC against 0.95**
   for the data-parallel version. The dataflow choice forced a worse inner
   loop.
2. **A block that scales with the problem starves the staging.** `mm/regblock`
   tied the per-tile block to the tile group, so at 256^2 the C block became
   512 words and left room only for the worst chunk depth. Fixing this --
   fixed-size blocks, iterated -- was worth **1.9x per-tile efficiency**.
3. **The systolic kernel cannot run the largest shape at all.** `mm/sysreg` at
   256^3 on a full pod exceeds the budget outright.

Every one of these is a buffer-allocation decision that a mapper with
data-lifetime information could make, and that a human got wrong first.

---

## 6. What a mapper would have to decide — with the measured optima

Each of these is a real decision with a non-obvious optimum. We found several
by sweeping, and got several wrong on the first attempt.

| decision | what we measured |
| --- | --- |
| Stage operands in scratchpad? | 8.5x fewer remote loads when yes |
| Block size | Must be **decoupled from tile count**; 1.9x per-tile |
| Block size vs load balance | Discrete and brittle: blk=16 at 128^3 leaves **half the pod idle** (64 blocks, 128 tiles) |
| Chunk depth | Monotonic: kb=16 > kb=8 > kb=4 (3985 / 4487 / 5109 cycles). Deeper bursts hide more latency |
| Message granularity | **Non-monotonic**: KC=4 beats KC=16. Opposite direction to chunk depth |
| Register tile shape | 4x4 is the max at 32 FP registers; 0.58 loads/MAC against a 0.5 floor |
| Operand dtype | int32 is **1.7x slower** than f32 -- register tile spills out of the GPRs |
| Forward ordering | Forward A before waiting on B, or a DRAM bubble at one feeder propagates into the other axis |

Note that chunk depth and message granularity move in **opposite** directions.
A mapper needs both a latency-hiding model and a pipeline-depth model, and
they conflict.

---

## 7. Compiler findings independent of the dataflow

- **`-ffp-contract=fast` does not fuse.** Disassembly showed separate `fmul.s`
  and `fadd.s`. Forcing `__builtin_fmaf` removed **26% of all instructions**.
  From the RTL (`fpu_float_fma.sv`): the FPU is a single FMA datapath where
  `fadd` is `rs1*1.0+rs2` and `fmul` is `rs1*rs2+0.0`, so an unfused
  multiply-accumulate burns two full passes to do one job. Any FP kernel on
  this machine is leaving a quarter of its instructions on the table.
- **Addressing mode has first-order effect.** Writing every access as a
  constant offset from one base pointer cut `addi` by **65%** and `bne` by
  66%. This is a codegen decision, not an algorithmic one.
- **The compiler already does some of the work.** Our first-chunk peel was
  predicted at 4% and delivered 1.9%, because GCC had already eliminated the
  zeroing pass by constant-folding through the unrolled loads. A mapper needs
  to model what the backend will do, or it will double-count its own wins.

---

## 8. Scaling, decomposed

Same kernel, same block size, 8 tiles vs 128:

| | MACs/cycle/tile | pod throughput |
| --- | --- | --- |
| 64^3 on 8 tiles | 0.3785 | 3.03 |
| 256^3 on 128 tiles | 0.2647 | 33.88 |

**11.2x throughput for 16x the tiles -- 70% scaling efficiency.** Per-tile
efficiency falls 1.43x, entirely attributable to contention.

Earlier in the study we measured a 128^3 run at blk=8 and computed 5.9x. That
figure conflates block size with scaling and should not be used: halving the
block halves compute-per-load independently of tile count.

A separate decomposition on the smaller run was exact: per-tile throughput fell
2.72x = **2.00x from block size** x **1.36x from contention** (effective cost
per DRAM load rose 21.1 -> 28.7 cycles). The model built from this predicted
the 256^3 result within **5%** before it ran.

---

## 9. Hardware mechanisms this data motivates

- **Completion tracking decoupled from transfer size** -- section 4. The one
  mechanism software provably cannot emulate.
- **Credits in hardware, per flow.** We hand-rolled per-flow credits in
  `mm/sysreg`; sharing one credit between the two flows would deadlock a
  dataflow that has no cycles. Easy to get wrong, mechanical to generate.
- **Row/column multicast.** `mm/sysreg` forwards A east through 15 tiles by
  store-and-forward, costing 40,960 payload stores at 128^3. A row multicast
  would make that one operation. The systolic pattern is a broadcast pattern
  wearing a chain costume.
- **Asynchronous block transfer.** The core currently parks on `lr_aq`. Wake
  is 1 cycle, but the *wait* is 52.6% of cycles. A transfer engine that lets
  the core continue is the difference between the two columns in section 2.

---

## 10. Methodology that worked, and that scales to the paper

- **One variant per directory, frozen baselines.** Seven matmul kernels, each
  with its own test suite. Every claim is an A/B at a fixed shape, never an
  attribution after the fact.
- **Pre-register the decision rule.** Before the systolic experiment we wrote
  down what each outcome would mean. It came back negative and we did not
  relitigate it.
- **Cheap sweeps kill expensive work.** Chunk double-buffering was refuted by
  a three-point sweep before it was built; message batching past KC=4 likewise.
  Both would have been days of implementation.
- **Offline verification catches everything except synchronization.** Values,
  coverage (every output written exactly once), resource budgets and
  compile-time guards all check offline. Handshake correctness does not --
  it needs hardware, walked up a ladder of tile-group sizes
  (1x1 -> 2x1 -> 1x2 -> 2x2 -> 4x2) so a hang localizes to the structure
  that rung introduced.
- **Tile-group size as a test parameter** is what makes multi-tile
  synchronization debuggable under RTL at all.

---

## 11. Workload coverage already in-tree

Thirteen kernels using `bsg_remote_ptr`, spanning most topologies of interest:

| pattern | kernels |
| --- | --- |
| 1D chain / systolic pipeline | `sw/1d`, `nw/baseline`, `nw/naive` |
| Ring | `sw/banded` |
| 2D wavefront | `sw/2d` |
| Bidirectional, direction-reversing | `nw/efficient` (Hirschberg) |
| All-to-all broadcast | `chaining/direct` |
| Log-depth tree with per-child credit | `chaining/tree` |
| 2D systolic, two flows | `mm/systolic`, `mm/sysreg` |
| Irregular / dynamic placement | `sw/scheduling`, `sw/dynamic` |
| Barrier algorithms | `barriers/` (linear, static tree, dynamic tree, amoadd) |
| Communication-free control | `mm/panel`, `mm/parallel` |

Of the proposal's candidate workloads, **alignment** (5 kernels) and
**matmul** (7) are done; **Floyd-Warshall** is the same wavefront dependency
family as alignment; **Cholesky** and **transport sweeps** share the
triangular-dependency structure.

---

## 12. What is missing

**No microbenchmark isolates communication.** Every number above comes from a
full kernel where communication is entangled with compute and memory.
`dummy/barrier_bench` is the only pure-communication benchmark in the tree.
Proposed `comm/` suite:

- `comm/p2p` -- latency and bandwidth vs Manhattan distance, payload size,
  concurrent pair count. The TRM's "2 cycles/hop + 4" is a no-load figure; the
  22% injection stall says the loaded curve is what matters.
- `comm/handshake` -- the full/credit round trip in isolation: single-slot vs
  double-buffered vs batched-window, against payload size.
- `comm/contention` -- N tiles injecting simultaneously, N from 1 to 128, to a
  neighbour vs to DRAM.
- `comm/patterns` -- chain, ring, tree reduce/broadcast, all-to-all, 2D
  systolic, with no compute at all.

Then fit a cost model on the microbenchmarks and **validate it against the
thirteen existing kernels**. That validation set is what turns a
characterization into a paper.

---

## 13. Honest caveats

- **RTL cost bounds the evaluation.** 256^3 on a full pod took ~5.5 hours.
  Larger shapes need silicon, and silicon access was unavailable for most of
  this study.
- **The baseline comparison is approximate.** Lin Cheng's thesis reports
  roughly 0.5M cycles for 256x256 on the same hardware; we measured 495,199.
  That "~0.5M" is read off a bar chart. Get the exact number before claiming
  parity in print.
- **One application.** The communication findings come almost entirely from
  matmul. The alignment kernels have the infrastructure but were not measured
  under the same lens.
