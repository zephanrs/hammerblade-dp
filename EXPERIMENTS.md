# HammerBlade Experiment Tracking

## Overview

Applications under study:
- `sw/1d` — Smith-Waterman, 1D systolic (baseline reference)
- `sw/2d` — Smith-Waterman, 2D tile-grid systolic
- `sw/dynamic` — SW with atomic-counter dynamic dispatch (variable-length)
- `sw/banded` — SW banded diagonal approximation
- `sw/scheduling` — SW with work-stealing-like scheduler (variable-length)
- `nw/naive` — Needleman-Wunsch storing full DP matrix
- `nw/baseline` — NW storing only final score
- `nw/efficient` — NW Hirschberg traceback (Divide & Conquer)
- `radix_sort` — 4-bit radix sort with tree prefix-sum

Target: Real 8-pod BSG cluster (16×8 tiles per pod, 8 pods total).

---

## Bug Inventory (Pass 1 — Analysis)

### ✅ `sw/1d` — CLEAN
- Multi-pod loop + `hb_mc_device_pods_kernels_execute` ✓
- Timing via `clock_gettime` ✓
- Repeat loop in kernel ✓
- Dense data layout ✓
- Status: **No changes needed for correctness. Tests.mk repeat counts may need scaling for 5 s target.**

### ✅ `nw/baseline` — MOSTLY CLEAN
- Multi-pod + timing ✓
- Status: **Tests.mk repeat counts need scaling for 5 s target.**

### ✅ `sw/banded` — MOSTLY CLEAN
- Multi-pod + timing ✓
- Status: **Tests.mk repeat counts need scaling for 5 s target.**

### ✅ `sw/2d` — MOSTLY CLEAN
- Multi-pod + timing ✓
- Validation only checks first sequence per pod — acceptable for benchmark
- Status: **Tests.mk repeat counts need scaling for 5 s target.**

---

### 🐛 `radix_sort` — MULTIPLE BUGS

**BUG R1 — `template.mk` line 3: hardcoded single-pod machine path**
```makefile
override BSG_MACHINE_PATH = $(REPLICANT_PATH)/machines/pod_X1Y1_ruche_X16Y8_hbm_one_pseudo_channel
```
This forces the simulator 1-pod machine for all runs. Must be removed for real hardware.
Fix: remove line; let `BSG_MACHINE_PATH` come from cluster environment.

**BUG R2 — `template.mk` line 20: wrong source file extension**
```makefile
TEST_SOURCES = main.cpp   # ← file is main.c, not main.cpp
```
The actual source is `main.c`. `compilation.mk` only picks up `.cpp` for this variable.
Fix: `TEST_SOURCES = main.c`

**BUG R3 — `template.mk`: missing `HB_HAMMERBENCH_PATH`**
All other apps define `HB_HAMMERBENCH_PATH ?= $(abspath $(APP_PATH)/../../../..)`.
`radix_sort/template.mk` never sets it but uses `$(EXAMPLES_PATH)` which depends on it.
Fix: add `HB_HAMMERBENCH_PATH ?= $(abspath $(APP_PATH)/../../../..)` after `include app_path.mk`.

**BUG R4 — `main.c`: old single-pod DMA API**
```c
hb_mc_device_dma_to_device(...)   // old name
hb_mc_device_dma_to_host(...)     // old name
hb_mc_device_tile_groups_execute(...)  // only launches current pod
```
Must use:
```c
hb_mc_device_transfer_data_to_device(...)
hb_mc_device_transfer_data_to_host(...)
hb_mc_device_pods_kernels_execute(...)   // launches all pods simultaneously
```

**BUG R5 — `main.c`: no kernel timing**
No `clock_gettime` around the kernel launch. Cannot collect performance data.

**BUG R6 — `main.c`: result read from wrong buffer after kernel**
The kernel does 8 sort passes (j=0,4,8,...,28). After an even number of swaps the data is back in the original `send` buffer (A). The host reads `A_device` — this is correct (8 is even). But needs verification if pass count changes.

**BUG R7 — `main.c`: no repeat loop, too small for DRAM bandwidth test**
For cache-flush / DRAM bandwidth test, need >128 kB data. Need both a large SIZE and a repeat mechanism since radix_sort has no inner repeat loop in the kernel.

**FEATURE R8 — barrier override for linear_barrier**
Need a way to compile the kernel with `barriers/linear_barrier.S` in place of the default `bsg_barrier_hw_tile_group_sync`. Plan: add `BARRIER_OVERRIDE` make variable.

---

### 🐛 `sw/scheduling` — BUGS

**BUG S1 — `template.mk` line 8: hardcoded single-pod machine path**
```makefile
override BSG_MACHINE_PATH = $(REPLICANT_PATH)/machines/bigblade_pod_X1Y1_ruche_X16Y8_hbm_one_pseudo_channel
```
Fix: remove line.

**BUG S2 — `main.cpp`: missing kernel timing**
`hb_mc_device_pods_kernels_execute` is called without `clock_gettime` before/after.
Fix: wrap with `clock_gettime` + `print_kernel_launch_time`.

**BUG S3 — `main.cpp`: missing `#include "../../common/host_bench.hpp"`**
`print_kernel_launch_time` is from `host_bench.hpp` but it's not included.
Fix: add include.

---

### 🐛 `sw/dynamic` — BUGS

**BUG D1 — `kernel.cpp` lines 135, 149: wrong stride for array access**
```cpp
uint8_t *ref_src = &ref[ref_len * input_id + (CORE_ID * ref_core)];  // WRONG
qry_char = qry[qry_len * input_id + i];                               // WRONG
```
The host sends data in dense (fixed-stride) layout with stride `MAX_SEQ_LEN`. But the kernel uses the variable per-sequence length `ref_len`/`qry_len` as the stride. For fixed-length sequences this happens to be the same value, but it is semantically wrong and will break for variable-length inputs.
Fix: use `MAX_SEQ_LEN` as the stride:
```cpp
uint8_t *ref_src = &ref[MAX_SEQ_LEN * input_id + (CORE_ID * ref_core)];
qry_char = qry[MAX_SEQ_LEN * input_id + i];
```

**BUG D2 — `main.cpp`: sends packed data but kernel expects dense layout**
```cpp
pack_variable_stride_sequences(query, qry_lens, num_seq, seq_len, packed_query);
htod_job.push_back({d_query, packed_query, ...});
```
The `pack_variable_stride_sequences` computes variable-offset packed layout, but the kernel indexes with a fixed-stride assumption. These are inconsistent.
Fix: remove packing entirely; send the dense `query` and `ref` arrays directly.
Remove `packed_query`, `packed_ref`, and the `pack_variable_stride_sequences` calls.

---

### 🐛 `nw/efficient` — BUGS

**BUG E1 — `main.cpp`: path buffer scaled by repeat factor — OOM for large repeats**
```cpp
BSG_CUDA_CALL(hb_mc_device_malloc(&device, total_num_seq*seq_len*sizeof(int), &d_path));
int* actual_path = (int*) malloc(total_num_seq*seq_len*sizeof(int));
```
`total_num_seq = num_seq * repeat_factor`. For repeat=64, num_seq=32768, seq_len=32:
32768 × 64 × 32 × 4 B = 268 MB per pod, × 8 pods = beyond practical limits.
Fix: Allocate path as `num_seq * seq_len` only.

**BUG E2 — `kernel.cpp` line 497: path written with repeat-scaled index**
```cpp
path[(output_idx * SEQ_LEN) + (core_id * REF_CORE) + col] = split_points[col];
// output_idx = (repeat * NUM_SEQ) + seq_id
```
With the path buffer fixed at `num_seq * seq_len` entries, path must be written with unscaled index:
```cpp
path[(seq_id * SEQ_LEN) + (core_id * REF_CORE) + col] = split_points[col];
```
Repeat iterations overwrite the same locations — acceptable since validation only checks first pass.

**BUG E3 — `main.cpp` validation: reads `actual_path` with wrong size for repeat**
`validate_exported_path` is called for indices 0..num_seq-1. With the path buffer fix (E1/E2), this is correct.

**NOTE E4 — `kernel.cpp`: `active_cores` loop starts at 4 — tied to `bsg_tiles_Y=8`**
The loop `for (int active_cores = 4; active_cores > 1; active_cores >>= 1)` starts at 4.
This is hardcoded for `bsg_tiles_Y = 8` (4 forward + 4 backward). Will be wrong for other Y sizes.
Currently we always use 8 Y tiles so this is fine, but worth documenting.

---

### 🐛 `nw/naive` — BUGS

**BUG N1 — `main.cpp` line 113: stack VLA for DP matrix — stack overflow for large sequences**
```cpp
int H[seq_len+1][seq_len+1];
```
For seq_len=2048: (2049)² × 4 B ≈ 16 MB on the stack → guaranteed stack overflow.
Fix: use `std::vector<int>` with dynamic allocation.

**BUG N2 — `main.cpp`: DP matrix buffer scaled by repeat factor — OOM**
```cpp
BSG_CUDA_CALL(hb_mc_device_malloc(&device, total_num_seq*matrix_size*sizeof(int), &d_dp_matrix));
```
For repeat=64, num_seq=2048, seq_len=32: 64 × 2048 × 33² × 4 B ≈ 1.2 GB per pod.
Fix: allocate as `num_seq * matrix_size` only.

**BUG N3 — `kernel.cpp` line 58: output index uses repeat-scaled `output_idx`**
```cpp
int *seq_dp = &dp_matrix[output_idx * DP_STRIDE * DP_STRIDE];
// output_idx = (repeat * NUM_SEQ) + s
```
With fix N2, kernel must wrap: `&dp_matrix[s * DP_STRIDE * DP_STRIDE]` (no repeat offset).

---

## Tests.mk Scaling for 5 s Target

The user confirmed ~20M repeats of 32×32 ran ≈ 5 s for `sw/1d`. Goal: all apps run ≥ 5 s.

Estimate: 20M iter / 5 s = 4M iter/s for seq_len=32.
- Throughput scales approximately as O(seq_len²) per iteration.
- For seq_len=L: repeats needed ≈ 20M × (32/L)²

New test sizes for performance runs (separate from correctness tests):

### sw/1d performance tests
Existing tests look reasonable. Need to verify with cluster timing.

### nw/baseline performance tests
Similar structure to sw/1d.

### nw/naive performance tests
Naive stores full matrix — much slower per sequence. Need fewer sequences.
Cannot run with repeat factor > 1 until OOM bugs fixed.

### nw/efficient performance tests
Complex traceback, slower per-sequence than sw/1d.

### sw/dynamic, sw/scheduling performance tests
Dynamic dispatch overhead means fewer sequences may be needed.
Repeat loop in dynamic kernel uses `total_num_seq` counter.

### sw/banded performance tests
Only traces a diagonal band — faster than full SW.

### radix_sort performance tests
No inner kernel repeat — use large SIZE (e.g., 4M elements = 16 MB > 128 kB).
Need to run kernel multiple times from host, or add repeat counter.

---

## Radix Sort Barrier Comparison

Two variants:
1. **Base barrier**: `bsg_barrier_hw_tile_group_sync` (built-in hardware barrier)
2. **Linear barrier**: `barriers/linear_barrier.S` (software AMOADD barrier)

Plan:
- Add `BARRIER_SRC` variable to `radix_sort/template.mk`
- If `BARRIER_SRC` is set, compile that `.S` file alongside kernel and provide override symbol

The `radix_sort/kernel.cpp` calls `bsg_barrier_hw_tile_group_sync`. To swap to linear:
- Add `EXTRA_BARRIER_OBJ` to `RISCV_TARGET_OBJECTS` that provides a wrapper
- Or: rename the symbol in `linear_barrier.S` to override the default

Since `linear_barrier.S` exports `bsg_barrier_amoadd` (not `bsg_barrier_hw_tile_group_sync`), need a thin wrapper or rename. Simplest approach: add a `custom_barrier.S` that re-exports `bsg_barrier_amoadd` as `bsg_barrier_hw_tile_group_sync` via a trampoline, and link it when `USE_LINEAR_BARRIER=1`.

---

## Cluster Configuration Notes

- 8 pods: `hb_mc_device_foreach_pod_id` iterates over all available pods automatically
- BSG_MACHINE_PATH must be set from the cluster environment (do NOT override in template.mk)
- Slow/half-speed mode: set via `BSG_MACHINE_HALF_SPEED=1` or per-machine-path; add `HALF_SPEED` flag to run scripts

---

## Run Script Plan

`run_experiments.sh`:
1. For each app × each test configuration:
   - Build (make in test directory)
   - Run at full speed → capture `kernel_launch_time_sec=` from stdout
   - Run at half speed → capture timing
2. Output CSV: `app, variant, seq_len, num_seq, repeat, speed_mode, kernel_time_sec`

---

## Status

| App | Correctness Bugs | Performance Config | Scripts |
|-----|------------------|--------------------|---------|
| sw/1d | ✅ Clean | TODO | TODO |
| sw/2d | ✅ Clean | TODO | TODO |
| sw/banded | ✅ Clean | TODO | TODO |
| nw/baseline | ✅ Clean | TODO | TODO |
| sw/dynamic | 🔴 D1, D2 | TODO | TODO |
| sw/scheduling | 🔴 S1, S2, S3 | TODO | TODO |
| nw/naive | 🔴 N1, N2, N3 | TODO | TODO |
| nw/efficient | 🔴 E1, E2, E3 | TODO | TODO |
| radix_sort | 🔴 R1–R7 | TODO | TODO |
