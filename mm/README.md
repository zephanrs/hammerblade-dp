# mm

Matrix multiply, `C = A * B`, targeting the real ASIC on the BSG cluster.

Variants:

- `single` — tile (0,0) computes the whole product. This is the single-core
  baseline everything else gets measured against.

## Parameters

Each test directory is one compile-time configuration, named
`m_<M>__n_<N>__k_<K>__dtype_<f32|i32>`. A is `M x K`, B is `K x N`, C is
`M x N`, all row-major in DRAM.

`dtype` selects the element type through `ELEM_IS_FLOAT` and `elem.hpp`:

- `f32` — `float`, using the per-core FPU.
- `i32` — `int`, using the M extension's pipelined multiplier.

Both dtypes run the identical kernel; only the element type changes.

The host fills A and B with small integers in `[-8, 8)` for both dtypes, so
every partial sum is an integer bounded by `K * 64`. That is far inside int32
and far below `2^24`, so single precision also represents the whole computation
exactly. The result check is therefore exact for both dtypes, and for `f32` it
stays exact whether or not the compiler contracted `a*b+c` into `fmadd.s`.
Switch to fractional operands and a relative tolerance if the point becomes
exercising floating-point rounding rather than validating the mapping.

## Installing into the app tree

Nothing special: this app ships inside the repo, which is cloned whole into the
cluster's app directory as the top-level README describes. `app_path.mk`
derives `APP_PATH` from `git rev-parse --show-toplevel`, so it resolves
correctly as long as `mm/single` sits inside a checkout of this repo.

`template.mk` assigns no `BSG_MACHINE_PATH` at all, like every other app on
this branch, so hardware picks it up from the cluster environment
(`bigblade_pod_X4Y2_ruche_X16Y8_fpga`, `bigblade-fpga`). There is no hot patch
to redo after a stash or re-clone, and nothing to re-comment.

`run_experiments.sh` does not cover `mm`: it dispatches a fixed registry of
named experiments, and no entry maps to this app. Run `mm` by hand as below,
or wire an entry into that registry.

`mm` is deliberately absent from `applications.mk`. CI runs `make native` over
that list against `hammer-sim`, which does not provide `bsg_pr_test_info`, so
registering `mm` there would fail the build. `make sim APP=mm` is unavailable
for the same reason; invoke the RTL flow directly, as below.

## Running under RTL simulation

RTL gives cycle counts and a PC histogram, which the microsecond wall-clock
from silicon does not. Unlike hardware, it needs an explicit machine.

```sh
module load hammerblade
cd <clone>/mm/single
make generate
make -C m_8__n_8__k_8__dtype_f32 profile.log \
  BSG_MACHINE_PATH=$REPLICANT_PATH/machines/bigblade_pod_X1Y1_ruche_X16Y8_hbm_one_pseudo_channel
```

`profile.log` carries the cycle counts between `bsg_cuda_print_stat_kernel_start`
and `_end`; `pc-histogram.log` shows where those cycles went, which is the
quickest way to see whether the inner loop is stalling on DRAM or retiring
multiply-accumulates.

Keep to the small shapes here. `8x8x8` is 512 MACs and turns around quickly;
`16x16x16` is 4096 and is about the practical ceiling for a quick iteration.
The `32^3` and `64^3` shapes are sized for silicon and will take a long time
under RTL.

## Running on hardware

Reset the unit first, coordinating with anyone else using it:

```sh
cd /cluster_src/reset_half
make reset UNIT_ID=2
```

Then generate the test directories and run one:

```sh
cd <apps>/mm/single
make generate
cd m_32__n_32__k_32__dtype_f32
make exec.log HB_MC_DEVICE_ID=2
```

A passing run ends with `BSG REGRESSION TEST PASSED`. The host library prints
the kernel execution time in microseconds.

Housekeeping:

- `make clean` in the test directory before rerunning the same test.
- `make purge` in the app directory to drop the generated test directories;
  rerun `make generate` after editing `tests.mk` or `template.mk`.
- If a run hangs or fails, kill it and redo the reset before running anything
  else.
- `make cool_down UNIT_ID=2` when finishing up, or before leaving the
  unit idle for more than an hour.

## What this is running on

From the HammerBlade silicon paper (TVLSI 2026) and the technical reference
manual, for sizing future variants:

- Vanilla core: RV32IMAF, each with its own FPU, 4 KB DMEM (1024 words) and
  4 KB i-cache. 2048 cores at 1.49 GHz.
- Up to 31 outstanding non-blocking loads per core, so the inner loop needs
  several independent accumulator chains to keep the pipeline fed.
- Local scratchpad: 2-cycle latency, 1 word/cycle.
- Another tile in the same group: 2 cycles per hop + 4, 1 word/cycle injection.
- DRAM: long latency, reached through the column victim caches; a miss pulls a
  16- or 32-word line, which is why the inner loop walks B with stride 1.
- The network guarantees point-to-point ordering between a given source and
  destination, so a payload store followed by a flag store arrives in order.
- The 4 KB scratchpad is the binding constraint: it also carries the stack, so
  `kernel.cpp` static_asserts that the resident C row stays well inside it.

A launch is always 4x2 pods of 16x8 tiles. Every pod runs the same kernel over
its own copy of the data, and the host validates each pod separately.
