# mm — implementation plan

Progression from one tile to a full 2-D systolic array, one variant per
directory, each keeping the previous as a baseline to measure against.

Every stage is a new directory. Nothing is edited in place, so every number
stays reproducible and regressions are visible.

**Status.** Stages 1–3 pass under RTL. Stage 4 is being built.

**Ordering decision.** Dataflow before inner-loop tuning: stage 4 (systolic)
precedes register blocking, even though register blocking is the larger single
lever, so the dataflow win is measured against a consistent baseline and the
inner kernel is rewritten once against its final structure.

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

## Stage 3 — `mm/parallel` (passing under RTL)

Verified offline: values exact both dtypes, full coverage of C with no gaps
or overlaps across 10 tile/shape configurations, scratchpad within budget,
all four static_asserts confirmed firing.

**Measured, 8x8x8 f32 on a 2x2 group:** 2184 cycles against 19973 for
`mm/single` at the same shape, a 9.1x improvement; core utilization 0.43% ->
48.78%. Counters matched the design exactly: 256 remote loads = 4 tiles x
(MB*KB + KB*NB), `fmul` 512 = M*N*K, `remote_fsw_dram` 64 = M*N.

Three findings:

1. **No FMA contraction.** `fadd` 512 *and* `fmul` 512 -- every
   multiply-accumulate is still two FP instructions, so `-ffp-contract=fast`
   did not take. Disassembly confirms it: four independent `fmul.s` followed
   by four `fadd.s`, with `b[0..3]` correctly hoisted into registers. The
   hardware has `eFMADD` (confirmed in `fpu_float_fma.sv`), so this is a
   toolchain question, not an ISA one.

   **Deferred by decision.** Because the compiler batches four independent mul
   chains before the adds, the first `fmul` has reached writeback by the time
   the first `fadd` issues -- so the missing fusion costs *issue slots, not
   stalls*. That bounds the win at 512 of 4214 instructions (~12%), and under
   ~6% of cycles given ~50% are stall. Both larger levers below come first.
   When revisited: check the flag reaches the RISC-V compile at all (it may be
   dropped or overridden by a later include), then try `__builtin_fmaf`, which
   is safe here since the hardware FMA is confirmed and cannot degrade to a
   libcall.
2. **Chunk staging is not overlapped with compute.** At 8^3/2x2,
   `stall_depend_dram_seq_load` was 42%; at 16^3/4x2 it fell to 26% and
   utilization rose 48.8% -> 60.4%, so part of the 8^3 figure was shape
   artifact -- but 26% is real. The kernel stages a chunk, computes on it,
   then stages the next, strictly serial. `stall_remote_req` also grew
   0.7% -> 9.1% as 8 tiles contended for network injection. Remote loads were
   exactly 1536 = 8 x (MB*K + K*NB), confirming the 12x redundancy figure.
   The systolic stage addresses this structurally: only the 23 edge tiles
   touch DRAM, the other 105 never do.
3. **Local FP memory traffic exceeds FP arithmetic**: `local_flw` 832 +
   `local_fsw` 768 = 1600 against 1024 FP ops. C is being loaded and stored
   from scratchpad on every single MAC. This is the register-blocking
   opportunity, and it is larger than expected -- see stage 5, where it should
   be promoted.

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

## Stage 4 — `mm/systolic` (built, awaiting RTL)

Verified offline: values exact both dtypes, full coverage of C, **zero DRAM
access from any interior tile**, and DRAM loads landing exactly on the ideal
`M·K + K·N` = 32768 at 128³ — 12.0× below `mm/parallel`, matching the
prediction. Kernel syntax-clean across 7 tile configurations; scratchpad guard
confirmed firing.

**What offline checking cannot cover:** the handshake. Credits, slot reuse and
forward ordering are timing-dependent and only testable on hardware. This is
why the test ladder starts at 1×1 (no neighbours), then 2×1 and 1×2 (one flow
in isolation), then 2×2 (every tile role present), then 4×2 (first genuinely
interior tiles). Walk it in that order; a hang at a given rung localises the
bug to the structure that rung introduced.

**Idea.** Output-stationary 2-D systolic array. Same C tiling as stage 3, but A
and B are never loaded redundantly: A flows west→east along tile rows, B flows
north→south along tile columns, and each tile forwards what it receives.

**Why output-stationary.** C is touched K times and is by far the most
expensive operand to move, so it stays put. A and B each traverse the array
exactly once. DRAM load traffic becomes `M·K + K·N`, optimal, and 12× below
stage 3 at 128³.

### Dataflow

Tile `(x,y)` owns the same C block as stage 3: rows `[y·MB, (y+1)·MB)`,
columns `[x·NB, (x+1)·NB)`. For each `k` in `0..K` it needs

- `a[0..MB)` = `A[row0 .. row0+MB, k]` — a *column* slice of its row slab
- `b[0..NB)` = `B[k, col0 .. col0+NB]` — a *row* slice of its column slab

and computes `c[i][j] += a[i] * b[j]`, i.e. exactly the rank-1 update the
earlier stages already use, so the inner kernel carries over unchanged.

### The strided-A problem

`a[0..MB)` is a column of A, so reading it straight from DRAM is stride-K —
one vcache line per element, the same mistake `mm/single` made. So the `x == 0`
tiles do **not** feed from DRAM per k. They stage an `MB × KB` chunk of their A
slab exactly as stage 3 does (each row a contiguous run, `unrolled_load`), then
feed columns out of scratchpad. `y == 0` tiles stage a `KB × NB` chunk of B the
same way; B's slices are already contiguous, but chunking keeps the two edges
symmetric.

### Per-k protocol, tile `(x,y)`

```
for kb in 0..K step KB:
    if x == 0: stage A chunk  (MB x KB) from DRAM
    if y == 0: stage B chunk  (KB x NB) from DRAM
    for kk in 0..KB:
        buf = kk & 1                          # double buffer
        a = (x == 0) ? &a_chunk[:, kk] : recv_west(buf)   # MB words
        b = (y == 0) ? &b_chunk[kk, :] : recv_north(buf)  # NB words
        if x < TGX-1: send_east(buf, a)       # forward BEFORE computing,
        if y < TGY-1: send_south(buf, b)      # so the pipeline fills
        for i in 0..MB:
            for j in 0..NB:
                c[i][j] += a[i] * b[j]
        release_credit_west(buf); release_credit_north(buf)
write c block to DRAM
```

Forwarding before computing is what lets tile `(x+1,y)` start its own k step
while `(x,y)` is still doing its `MB·NB` FMAs.

### Scratchpad accounting (128³, 16×8, MB=16, NB=8, KB=16)

| tile | contents | words |
| --- | --- | --- |
| interior | C block 128 + a_in 2·16 + b_in 2·8 | 176 |
| `x == 0` | + A chunk MB·KB | +256 |
| `y == 0` | + B chunk KB·NB | +128 |
| `(0,0)` | all of the above | **560** |

Under the 768-word budget, with room to grow MB/NB. Note every tile compiles
the same binary, so all tiles pay the edge buffers in static allocation — the
budget check must use the `(0,0)` figure, not the interior one.

### Feasibility

Per k step a tile does `MB·NB` FMAs and moves `MB+NB` words. At MB=16, NB=8
that is 128 FMAs per 24 words = **5.3 FMAs per word moved**. At one word/cycle
injection against one FMA/cycle issue, communication is ~19% of compute —
hideable with double buffering, so the array should stay compute-bound. If this
ratio ever drops below ~2 the array is communication-bound and MB/NB must grow.

Pipeline fill is `TGX + TGY - 1` = 23 hops deep. At a generous ~20 cycles per
hop including handshake that is ~460 cycles against `K·MB·NB` = 16384 FMA issue
slots, about 3%. Fill cost is not a concern at these shapes; it would be at
small K.

### Transport

The `nw/mailbox.hpp` pattern applies directly: payload stores then a flag store
(point-to-point network ordering makes this safe), receiver parks on
`bsg_lr`/`bsg_lr_aq`, credit flag back to the sender for backpressure.

**Credits must be per-flow.** A flows strictly +x and B strictly +y, both
feed-forward with no cycles, so there is no circular wait and no deadlock — but
a single shared credit would let a stalled B consumer block an A forward, which
reintroduces one artificially.

### Design decisions, and what was rejected

| decision | taken | rejected alternative |
| --- | --- | --- |
| stationary operand | C (output-stationary) | A- or B-stationary: C would then move K times, the worst choice |
| edge tiles | also compute | dedicating row 0 + column 0 as pure feeders costs 23 of 128 tiles (18%), worse than the ~12% edge imbalance it removes |
| message granularity | one k step per message | batching KC steps cuts flag overhead but multiplies buffering and deepens fill; revisit if handshake cost shows up |
| A delivery | store-and-forward | network multicast along the row — no hardware support, and store-and-forward keeps the credit scheme simple |

**Known imbalance.** `x == 0` tiles additionally load `MB·K` words of A and
`y == 0` tiles `K·NB` of B. The array runs at the speed of its slowest tile, so
expect the edges to gate throughput by roughly 10–15% at 128³. Worth measuring
per-tile before trying to fix.

## Stage 5 — optimizations on the systolic array

In rough order of expected value:

1. **Register-blocked inner kernel.** *Promoted to first on stage 3 evidence:*
   at 8³ the kernel issued 1600 local FP loads/stores against 1024 FP
   arithmetic ops, because `c[j] +=` round-trips scratchpad on every MAC. With
   32 FP registers a 4×4 C sub-tile lives entirely in registers — 16 for C, 4
   for a, 4 for b — giving 16 FMAs per 8 loads and no scratchpad traffic for C
   in the inner loop. Tile the MB×NB block into 4×4 sub-tiles. This applies to
   stage 3 as much as stage 4.
2. **Double-buffered inflow.** Receive k+1 while computing k. Directly targets
   the ~19% communication overhead.
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

- ~~Does the toolchain emit `fmadd.s` with `-ffp-contract=fast`?~~ **No** — at
  8³ stage 3 showed `fadd` 512 and `fmul` 512, one of each per MAC. The
  hardware supports it, so the question is now *why* the compiler will not
  emit it: is the flag reaching the RISC-V compile at all, is it being
  overridden by a later include, or does the backend need `__builtin_fmaf`?
- Does the kernel become FP-issue-bound once C lives in registers?
- Do 128 tiles reading overlapping A lines serialize in the vcache in stage 3?
- What is the real cost of a mailbox hop under load, versus the 2 cycles/hop + 4
  the TRM quotes?
