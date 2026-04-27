#!/usr/bin/env bash
# preflight.sh — probe suspect (cpg, seq_len) combos for sw/1d before launch.
#
# Why:  EXPERIMENTS.md's `sw1d_cpg_fast` adds many (cpg, seq_len) rows that
# have never run on real HW.  Prior tests.mk also marks several rows that
# previously timed out (cpg=2/8 at seq_len=512, cpg=16 at seq_len=128/256).
# Before launching the 50-run sweep, probe the suspect combos at repeat=1
# with a short timeout to detect hangs/timeouts cheaply.
#
# Usage:
#   ./preflight.sh                         # 120 s timeout per probe (default)
#   PREFLIGHT_TIMEOUT=60 ./preflight.sh    # tighter timeout
#
# Mechanism:
#   - Backs up sw/1d/tests.mk, swaps in a probe-only tests.mk
#   - Runs ./run_experiments.sh sw1d_preflight (registered in run_experiments.sh)
#   - Always restores the original tests.mk on exit (success / failure / Ctrl-C)
#
# Output: results/results_<timestamp>.csv with PASS / TIMEOUT / FAILED per probe.

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "$0")" && pwd)"
APP_DIR="$REPO_ROOT/sw/1d"
TESTS_MK="$APP_DIR/tests.mk"
TESTS_BAK="$APP_DIR/tests.mk.preflight.bak"

# ── Restore tests.mk on any exit ─────────────────────────────────────────────
restore_tests_mk() {
  if [ -f "$TESTS_BAK" ]; then
    mv -f "$TESTS_BAK" "$TESTS_MK"
    echo "[preflight] restored sw/1d/tests.mk"
  fi
}
trap restore_tests_mk EXIT INT TERM

# ── Stash original tests.mk ──────────────────────────────────────────────────
if [ -f "$TESTS_BAK" ]; then
  echo "[preflight] ERROR: $TESTS_BAK already exists — refusing to overwrite." >&2
  echo "[preflight] If you're sure no preflight is in flight, remove it manually." >&2
  trap - EXIT INT TERM
  exit 1
fi
cp "$TESTS_MK" "$TESTS_BAK"
echo "[preflight] backed up $TESTS_MK -> $TESTS_BAK"

# ── Write probe tests.mk ─────────────────────────────────────────────────────
# Each probe runs at repeat=1 and asks only "does this combo complete?".
# DMEM constraint: REF_CORE = seq_len / cpg ≤ 256.
# FASTA constraint: num_seq × seq_len ≤ 1,048,576 → num_seq = 1M / seq_len.
cat > "$TESTS_MK" <<'EOF'
# preflight: suspect-row probes at repeat=1 for sw/1d.
# Generated transiently by preflight.sh — original tests.mk restored on exit.

# ── Known-good controls (sanity check the probe harness) ────────────────────
TESTS += seq-len_256__num-seq_4096__repeat_1
TESTS += seq-len_2048__num-seq_512__repeat_1__cpg_8

# ── Previously-timed-out rows (regression probes) ───────────────────────────
TESTS += seq-len_512__num-seq_2048__repeat_1__cpg_2
TESTS += seq-len_512__num-seq_2048__repeat_1__cpg_8
TESTS += seq-len_128__num-seq_8192__repeat_1__cpg_16
TESTS += seq-len_256__num-seq_4096__repeat_1__cpg_16

# ── New CPG=16 launch rows (small + large extremes only) ────────────────────
TESTS += seq-len_64__num-seq_16384__repeat_1__cpg_16
TESTS += seq-len_512__num-seq_2048__repeat_1__cpg_16
TESTS += seq-len_4096__num-seq_256__repeat_1__cpg_16

# ── New CPG=32 launch rows (whole tier untested) ────────────────────────────
TESTS += seq-len_128__num-seq_8192__repeat_1__cpg_32
TESTS += seq-len_512__num-seq_2048__repeat_1__cpg_32
TESTS += seq-len_8192__num-seq_128__repeat_1__cpg_32

# ── New CPG=64 launch rows (whole tier untested) ────────────────────────────
TESTS += seq-len_256__num-seq_4096__repeat_1__cpg_64
TESTS += seq-len_1024__num-seq_1024__repeat_1__cpg_64
TESTS += seq-len_16384__num-seq_64__repeat_1__cpg_64

# ── New CPG=128 launch rows (256, 512 already covered in original tests.mk) ─
TESTS += seq-len_1024__num-seq_1024__repeat_1__cpg_128
TESTS += seq-len_4096__num-seq_256__repeat_1__cpg_128
TESTS += seq-len_32768__num-seq_32__repeat_1__cpg_128
EOF

# ── Launch ───────────────────────────────────────────────────────────────────
TIMEOUT="${PREFLIGHT_TIMEOUT:-120}" \
  "$REPO_ROOT/run_experiments.sh" sw1d_preflight

# tests.mk restored by trap on EXIT
