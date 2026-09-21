# mm — implementation plan

Progression from one tile to a full 2-D systolic array, one variant per
directory, each keeping the previous as a baseline to measure against.

Every stage is a new directory. Nothing is edited in place, so every number
stays reproducible and regressions are visible.

## Hardware budget (the constraints every stage designs against)

| resource | value | consequence |
| --- | --- | --- |
| tiles per pod | 16 (x) × 8 (y) = 128 | the decomposition has to be 2-D to use them all |
| DMEM | 1024 words, shared with stack | budget ~768 words of data per tile |
| icache | 1024 instructions | aggressive unrolling is a real limit, not a style choice |
| FP regs | 32 | a 4×4 register-blocked C tile is the natural inner kernel |
| FMA | 2-stage pipelined, ~3-cycle dependent latency | need ≥3 independent chains; 8 is comfortable |
| issue | single-issue, in-order | instruction count is a hard floor on cycles |
| local load | 2 cycles | scratchpad is ~10× cheaper than a remote hit |
| neighbour tile | 2 cycles/hop + 4 | nearest-neighbour is much cheaper than DRAM |
| remote load (measured) | ~21 cycles avg, mostly vcache hits | this is what stages 2–4 are fighting |
| outstanding loads | 31 | issue loads early, consume late |

## Stage 1 — `mm/single` (done)

One tile, i-k-j rank-1 update, C row resident in scratchpad, A broadcast across
the inner loop, k=0 peeled.

Measured at 8×8×8 f32 under RTL: 19973 cycles, and the instruction counts
matched the design exactly (`fmul` 512 = M·N·K, `fadd` 448 = M·N·(K−1),
`remote_flw_dram` 576 = M·K·N(B) + M·K(A), `remote_fsw_dram` 64 = M·N).

**What it taught us:** tile (0,0) spent ~61% of cycles in
`stall_depend_dram_load`. 576 remote loads, nearly all vcache *hits*, each
costing a network round trip. The loop order was right for DRAM traffic and
wrong for network traffic.

## Stage 2 — `mm/blocked` (done, passing under RTL)

Same single tile, but each column panel of B is staged into scratchpad once and
the inner loop reads it locally. `BLK_N` is a test parameter; `K·BLK_N + BLK_N`
must fit the budget.

Remote loads counted offline: 4.5× fewer at 8³, 8.5× at 16³, 11.0× at 32³.

Also enables `-ffp-contract=fast` on both variants. The FPU is a single FMA
datapath where `fadd` is `rs1*1.0+rs2` and `fmul` is `rs1*rs2+0.0`, so an
uncontracted multiply-accumulate burns two passes where one `fmadd.s` does the
job at identical latency.

**Open question this stage answers:** with remote loads cut ~8×, does the
kernel become FP-issue-bound? If `stall_depend_dram_load` collapses and core
utilization climbs, the single-tile story is finished and parallelism is the
only remaining lever.

## Stage 3 — `mm/parallel` (built, awaiting measurement)

**Idea.** Tile the output across all 128 tiles. No inter-tile communication at
all — just the existing entry/exit barriers.

**Mapping.** Tile `(x,y)` owns the C block of rows `[y·MB, (y+1)·MB)` and
columns `[x·NB, (x+1)·NB)`, where `MB = M/8` and `NB = N/16`. It accumulates
that block in scratchpad across the whole of K, loading the A and B sub-blocks
it needs directly from DRAM.

K is processed in chunks of `KB` so the working set fits:

```
C block   MB·NB      resident across all of K
A block   MB·KB      reloaded per k-chunk
B block   KB·NB      reloaded per k-chunk
                     MB·NB + MB·KB + KB·NB <= 768
```

For M=N=K=128: MB=16, NB=8, KB=16 → 128 + 256 + 128 = 512 words.

**Why 2-D and not just splitting columns.** A 1-D column split needs
`N/BLK_N >= 128` panels to keep every tile busy — N ≥ 1024 with BLK_N=8. The
2-D split keeps all 128 tiles busy from N=128, M=64.

**Known inefficiency, deliberately left in.** Each A sub-block is loaded by all
16 tiles in its row, each B sub-block by all 8 tiles in its column. DRAM *load*
traffic is `16·M·K + 8·K·N` — 393216 words at 128³ against an ideal 32768, i.e.
**12× redundant** (verified by counting in the offline harness). Counting the
unavoidable `M·N` of C stores on both sides dilutes it to 8.3×, but loads are
what stage 4 removes, so 12× is the figure to beat. That redundancy is exactly
what stage 4 removes, and measuring it here is what makes stage 4's gain
legible.

**Optimizations.** Reuse the stage-2 inner loop unchanged. Peel k=0 per
k-chunk. Load A/B sub-blocks with `unrolled_load` so round trips overlap.

**Risk.** 128 tiles hitting the same vcache lines for A may serialize. If
`stall_depend_dram_load` does not fall roughly in proportion to the tile count,
that is the reason, and it argues for going straight to stage 4.

## Stage 4 — `mm/systolic` (the target)

**Idea.** Output-stationary 2-D systolic array. Same C tiling as stage 3, but A
and B are never loaded redundantly: A flows west→east along tile rows, B flows
north→south along tile columns, and each tile forwards what it receives.

**Per k step, tile `(x,y)`:**

1. receive `a[0..MB)` from the west (or load from DRAM if `x == 0`)
2. receive `b[0..NB)` from the north (or load from DRAM if `y == 0`)
3. forward `a` east, `b` south — *before* computing, so the pipeline fills
4. `c[i][j] += a[i] * b[j]` for all `MB·NB` pairs

**Why output-stationary.** C never moves, so there is no accumulation traffic —
the single most expensive thing to move, since it is touched K times. A and B
each traverse the array exactly once. DRAM traffic becomes `M·K + K·N + M·N`,
optimal, and 12× below stage 3 on loads at 128³.

**Feasibility check.** Per k step a tile does `MB·NB` FMAs and moves `MB+NB`
words. At MB=16, NB=8 that is 128 FMAs per 24 words = **5.3 FMAs per word
moved**. At one word/cycle injection and one FMA/cycle issue, communication is
~19% of compute — hideable with double buffering, so the array should stay
compute-bound. This ratio is the number to check first; if it drops below ~2
the array is communication-bound and MB/NB need to grow.

**Transport.** The `nw/mailbox.hpp` pattern applies directly: payload stores
then a flag store (point-to-point network ordering makes this safe), receiver
parks on `bsg_lr`/`bsg_lr_aq`, credit flag back to the sender for
backpressure. Two independent flows per tile (east and south), so two mailbox
pairs.

**Scratchpad.** C block MB·NB=128, plus double-buffered a/b inflow
2·(MB+NB)=48 → ~176 words. Comfortable, which leaves room to grow MB/NB.

**Deadlock risk.** Two flows on a dimension-ordered network with finite
buffering. A flows purely in +x, B purely in +y, and both are strictly
feed-forward with no cycles, so there is no circular wait — but the credit
scheme has to be per-flow, or a stalled B consumer can block an A forward.

## Stage 5 — optimizations on the systolic array

In rough order of expected value:

1. **Double-buffered inflow.** Receive k+1 while computing k. Directly targets
   the ~19% communication overhead.
2. **Register-blocked inner kernel.** With 32 FP registers, a 4×4 C sub-tile
   lives entirely in registers: 16 for C, 4 for a, 4 for b. 16 FMAs per 8
   loads, and no scratchpad traffic for C in the inner loop. Tile the MB×NB
   block into 4×4 sub-tiles.
3. **icache discipline.** Check the unrolled body against the 1024-instruction
   limit. Keep `b[0..NB)` in registers across the i loop rather than unrolling
   both dimensions.
4. **Multi-pod.** Every launch is 4×2 pods currently doing identical redundant
   work. Split M or N across the 8 pods for a further 8×.
5. **`repeat` / `INPUT_REPEAT_FACTOR`.** Needed before any silicon timing is
   quotable; matches the rest of the branch.

## Testing strategy

**Tile group size must become a test parameter.** `template.mk` already takes
`tile-x`/`tile-y`. Adding `tgx_`/`tgy_` to the test name lets RTL validate the
systolic logic on a 2×2 or 4×2 group with small matrices — minutes, not hours —
before scaling to 16×8 on silicon. Without this, stages 3–4 cannot be debugged
under RTL at all.

**Shapes.** RTL keeps small shapes for correctness. Silicon takes the large
ones where 128 tiles have real work (M ≥ 64, N ≥ 128).

**Correctness stays exact.** Operands are small integers in both dtypes, so
every partial sum is exactly representable and comparison is bit-exact
regardless of dtype, FMA contraction or accumulation order. Preserve this
property in every stage — it is what makes a mismatch always a real bug.

## Metrics to record per stage

Per variant, per shape, both dtypes:

- `Runtime` cycles, and cycles per MAC (= runtime / (M·N·K))
- `remote_flw_dram` / `remote_fsw_dram` counts
- `stall_depend_dram_load` as a fraction of the working tile's cycles
- `fmul` / `fadd` / `fmadd` split (confirms contraction)
- `stall_bypass` (confirms enough independent FMA chains)
- DRAM utilization, vcache miss rate, core utilization
- for stages 3–4: the above for a working tile, not the pod average — the
  aggregate counters are dominated by idle tiles parked in `stall_lr_aq`

## Open questions

- Does stage 2 become FP-issue-bound, or is there still remote-load stall?
- Does the toolchain actually emit `fmadd.s` with `-ffp-contract=fast`, or does
  the vanilla FPU not expose it through the compiler?
- Do 128 tiles reading overlapping A lines serialize in the vcache in stage 3?
- What is the real cost of a mailbox hop under load, versus the 2 cycles/hop + 4
  the TRM quotes?
