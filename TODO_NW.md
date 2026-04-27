# nw/* — open work

## 1. Hardware cliff (resolved as a constraint)

`nw/efficient` (and any kernel that does per-iteration DRAM writes indexed
by `seq_id`) hangs when **iterations per column** (= `num_seq / bsg_tiles_X`
= `num_seq / 16`) is an integer multiple of **32**. Equivalently: hangs when
`num_seq` is an integer multiple of 512.

Confirmed across `seq_len ∈ {32, 64, 128}`. seq_len=32 fails at iters/col
∈ {32, 64, 96}; seq_len=64 and 128 fail at iters/col=256 (= 8×32). Every
non-multiple-of-32 iters/col passes regardless of total scale (tested up
to ~32k sequences per pod, ~2k iters/col).

`nw/baseline` does NOT appear affected — it only writes one int per
sequence to DRAM (no per-cell writes). Tested at `num_seq=32768` (= mul
of 512) → passed.

**Working hypothesis** for the mechanism: a 32-deep on-chip resource
(MSHR queue / wormhole router buffer / cache request table) that
synchronizes across the 16 vcaches per pod when path-write traffic
recurs at exactly the same hash positions. The 4-way 64-set vcache with
IPOLY hashing makes simple "cache thrashing" insufficient as an
explanation, since the cliff is at exact multiples (not "more than"),
which a capacity story doesn't fit.

**Workaround used everywhere**: pick `num_seq` that is a multiple of 16
(barrier requirement) but NOT a multiple of 512.

## 2. Test calibration to ~20s per run — TODO

Current `nw/{baseline,efficient,naive}/tests.mk` use `repeat=1` to
collect kernel_us per (seq_len, num_seq). Once we have those numbers,
scale `repeat` per row so each test hits ~20s wall time:

```
repeat = round(20 / kernel_seconds_at_repeat_1)
```

Make sure `repeat × num_seq / 16` does not become a multiple of 32 (the
cliff applies to total barrier sequence count, not per-repeat count, as
far as we know — confirm before running).

## 3. CPG support for nw/efficient — TODO

Adding CORES_PER_GROUP support to nw/efficient like sw/1d has, in tiers
of effort:

### Cheap (1-2 hours): cpg ∈ {2, 4, 8} — subgroups of one column

- Replace `bsg_tiles_Y` macro references with `CORES_PER_GROUP` (default
  `bsg_tiles_Y` to preserve current behavior).
- `REF_CORE = SEQ_LEN / CORES_PER_GROUP`.
- Active-cores tournament loop start: `for (active_cores = CORES_PER_GROUP/2; active_cores > 1; active_cores >>= 1)`.
- Mailbox neighbors stay `__bsg_y ± 1` because cpg ≤ 8 means subgroups
  fit inside one column.
- Forward/backward team partitioning logic already works for any
  power-of-2 active_cores.
- `init_mailboxes()` in `mailbox.hpp` keeps using `__bsg_y ± 1`.

### Moderate (1-2 days): cpg ∈ {16, 32, 64, 128} — groups span columns

Everything from cheap, plus:
- Switch all `bsg_remote_ptr(__bsg_x, __bsg_y ± 1, …)` calls to use 1D
  tile IDs:
  ```c
  #define MY_TILE_ID  (__bsg_x * bsg_tiles_Y + __bsg_y)
  #define TILE_X(id)  ((id) / bsg_tiles_Y)
  #define TILE_Y(id)  ((id) % bsg_tiles_Y)
  ```
  (matches sw/1d).
- `init_mailboxes()` becomes `init_mailboxes(prev_id, next_id)` and uses
  `TILE_X/TILE_Y(id)` to compute the neighbor coordinates. The
  `top`/`bottom` ports go away entirely (only `left`/`right` matter for
  1D pipelines).
- `parallel_fill` `neighbor_boundary_scores` uses the same 1D mapping
  for the cross-team peer.
- GROUP_ID / NUM_GROUPS / ACTIVE_COMPUTE_GROUPS plumbing same shape as sw/1d.

### Hard (week+): cpg = 1 — single-tile Hirschberg

Tear out the multi-core phases entirely; each tile handles a whole
sequence on its own. Probably not interesting enough to bother with.

## Recommended order

1. Run the `repeat=1` calibration sweep (already pushed).
2. Update tests.mk per app with calibrated `repeat` values targeting 20s.
3. Cheap CPG support (cpg ∈ {2, 4, 8}). Add cpg=4, cpg=2 entries to
   `nw/efficient/tests.mk` so we get a 3-point CPG comparison.
4. Decide whether to invest in the moderate CPG version based on results.
