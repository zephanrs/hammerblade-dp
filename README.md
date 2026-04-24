# hammerblade-smith-waterman

Smith-Waterman and Needleman-Wunsch sequence alignment on HammerBlade ASIC,
plus a parameterized roofline benchmark.

## Repository layout

```
sw/1d/           Smith-Waterman, 1-D tile layout (CORES_PER_GROUP sweep)
sw/2d/           Smith-Waterman, 2-D tile layout
nw/naive/        Needleman-Wunsch, naive reference
nw/baseline/     Needleman-Wunsch, baseline HammerBlade
nw/efficient/    Needleman-Wunsch, optimized HammerBlade
dummy/roofline/  Synthetic roofline benchmark (sweeps OPS_PER_ELEM)
common/          Shared host utilities (timing, repeat config)
run_experiments.sh   Experiment runner → results/
plot_roofline.py     Roofline plotter (reads CSV)
results/             CSV and log output — tracked by git for pull-back
```

---

## Step-by-step: running on the BSG hardware cluster

### 0. Connect and set up (first time only)

Connect via ZeroTier VPN (see your admin for network ID and private IP), then:

```sh
ssh <username>@192.168.196.XXX
```

Your assigned `UNIT_ID` is **2**.

Clone and set up the repo inside `bsg_bladerunner`:

```sh
cd bsg_bladerunner/bsg_replicant/hb_hammerbench/apps
git clone <this-repo> hammerblade-smith-waterman
cd hammerblade-smith-waterman
```

**Hot-patch `template.mk` in every app** — comment out the line that sets
`BSG_MACHINE_PATH` (the cluster machine config handles this):

```sh
# In sw/1d/template.mk, sw/2d/template.mk, nw/naive/template.mk, etc.:
# Comment out any line like:  BSG_MACHINE_PATH := ...
# Leave it blank or removed.
```

> You must redo this after any `git stash`, `git checkout`, or re-clone.

---

### 1. Reset the device (do this every time before running)

```sh
cd /cluster_src/reset_half
make reset UNIT_ID=2
```

Confirm the output matches the expected reset output.  Check temperatures are
below 40 °C.  If the output looks wrong, contact the admin before proceeding.

You can also run this from the repo root:

```sh
make reset
```

---

### 2. Run all experiments

```sh
cd bsg_bladerunner/bsg_replicant/hb_hammerbench/apps/hammerblade-smith-waterman
./run_experiments.sh
```

This will:
1. Generate test parameter directories for each app
2. Run `make clean` + `make exec.log HB_MC_DEVICE_ID=2` for each test
3. Parse timing from `exec.log`
4. Write `results/results_<timestamp>.csv` and `results/run_<timestamp>.log`

Run a subset of apps:

```sh
./run_experiments.sh sw/1d
./run_experiments.sh dummy/roofline
./run_experiments.sh nw/naive nw/baseline nw/efficient
```

Set a per-test timeout (default 3600 s):

```sh
TIMEOUT=600 ./run_experiments.sh
```

---

### 3. What to do if a test fails or hangs

The run script will:
1. Log the failure with the last 10 lines of make output
2. Record `FAILED` or `TIMEOUT` in the CSV
3. Automatically run `cd /cluster_src/reset_half && make reset UNIT_ID=2`
4. Continue with the next test

If the script itself hangs (Ctrl-C it):

```sh
make reset-device   # kills the process
make reset          # resets the hardware
```

Then re-run just the app that was running.  Tests that already passed are
already in the CSV — you can re-run only the remaining apps.

---

### 4. Cool down after finishing

Before logging out, or if you will be idle for more than 1 hour:

```sh
make cool-down
```

Confirm temperatures are below 40 °C.

> Note: `cool_down` also slows the core clock by 32×.  This is how you collect
> slow-clock roofline data (see Slow-clock section below).

---

### 5. Run a single test manually

```sh
cd sw/1d
make generate
make clean
make exec.log HB_MC_DEVICE_ID=2
cat seq-len_256__num-seq_512__cpg_8/exec.log
```

---

### 6. Slow-clock roofline data

To collect slow-clock (32× slower core) data for the roofline model:

```sh
# 1. Reset as normal:
make reset

# 2. Run cool_down to engage the slow clock:
make cool-down

# 3. Run experiments with SLOW_MODE=1 (results labeled "slow" in the CSV):
SLOW_MODE=1 ./run_experiments.sh dummy/roofline

# 4. Reset again to return to full speed:
make reset

# 5. Run full-speed experiments normally:
./run_experiments.sh
```

---

### 7. Pull results back to your laptop

```sh
# On the server:
git add results/
git commit -m "experiment results $(date +%Y%m%d)"
git push

# On your laptop:
git pull
python3 plot_roofline.py results/results_<timestamp>.csv --out roofline.png
```

---

## Quick reference: hardware workflow

```
Every run:
  reset → run experiments → cool-down (when done)

After failure/hang:
  Ctrl-C → make reset-device → make reset → re-run

Before leaving:
  make cool-down
```

---

## Make targets

```sh
make reset                   # reset hardware unit (UNIT_ID=2)
make cool-down               # cool down / slow clock
make reset-device            # kill processes + remind to reset
make status                  # show test dirs and running processes
make experiments             # run all experiments via run_experiments.sh

make clean                   # clean build artifacts
make clean-results           # delete results/ directory
make clean-generated         # delete all generated test directories
make clean-all               # all of the above
```

---

## Plotting the roofline

```sh
pip install matplotlib numpy   # first time only

python3 plot_roofline.py results/results_<timestamp>.csv --out roofline.png
```

The script uses `dummy/roofline` kernel data as the roofline anchors:
- **BW roof**: max `achieved_bw_GB_s` from low-OI rows (`ops_per_elem ≤ 4`)
- **Compute roof**: max `achieved_gops_s` from high-OI rows (`ops_per_elem ≥ 256`)
- **SW points**: plotted as (AI, GCUPS) scatter

---

## CSV columns

| Column | Description |
|---|---|
| `app` | App name, e.g. `sw/1d` |
| `test_name` | Test directory name (encodes all parameters) |
| `seq_len` | Sequence length (SW/NW tests) |
| `num_seq` | Sequences per pod per kernel call |
| `repeat` | Outer-loop repeat count |
| `cpg` | Cores per group (sw/1d only) |
| `pod_unique_data` | 1 = each pod uses unique sequences |
| `speed` | `fast` or `slow` (clock variant) |
| `kernel_time_sec` | Kernel wall-clock time in seconds |
| `gcups` | `num_seq × repeat × seq_len² / t / 1e9` |
| `arith_intensity_ops_per_byte` | Arithmetic intensity (ops/byte) |
| `ops_per_elem` | Roofline kernel: ops per element |
| `n_elems` | Roofline kernel: elements processed |
| `achieved_bw_GB_s` | Roofline kernel: measured DRAM bandwidth |
| `achieved_gops_s` | Roofline kernel: measured compute throughput |

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| Test hangs indefinitely | Ctrl-C, `make reset-device`, `make reset`, re-run |
| `FAILED` in CSV | Check `<test>/run.log`; run `make reset` before next run |
| `TIMEOUT` in CSV | Increase `TIMEOUT=N`; run `make reset` |
| Temperature > 40 °C | Contact admin immediately |
| Reset output looks wrong | Contact admin before running anything |
| Device unresponsive after failure | `make reset-device`, then `make reset` |
| Roofline BW peak is 0 | Run `./run_experiments.sh dummy/roofline` |
