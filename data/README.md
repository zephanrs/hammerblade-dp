# data/

Per-experiment HW results.  Each subdirectory holds the raw CSV(s) +
runtime log(s) for one named experiment from `EXPERIMENTS.md`.

| Dir | Notes |
|---|---|
| `sw1d_cpg_fast/` | Two source CSVs (initial run + re-run of failed rows).  `combined.csv` merges them: latest successful row per `test_name`, falling back to the latest failure if all attempts failed.  39 / 50 rows OK; 11 still TIMEOUT. |
| `sw2d_seqlen_fast/` | 6 / 6 rows OK |
| `nw_seqlen_fast/` | 6 / 6 rows OK (nw/baseline + nw/efficient) |
| `nw_naive_fast/` | 3 / 3 rows OK (nw/naive split out) |
| `roofline_fast/` | 32 / 32 rows OK |
| `radix_sort_fast/` | 18 / 18 rows OK |
| `vvadd/` | Diagnostic vvadd memory-bandwidth probe.  `fast.csv` and `slow.csv` (= "high bandwidth", see project conventions). |

Slow-clock runs not yet executed; they'll land here under
`sw1d_cpg_slow/`, `sw2d_seqlen_slow/`, `radix_sort_slow/`,
`roofline_slow/` once collected.

For "slow" runs the throughput columns (`gcups`, `achieved_bw_GB_s`,
`achieved_gops_s`) are **already sim32bw-projected** — that is, slow
time × 1/32 to model a fast-clock machine with 32× more memory
bandwidth.  See `EXPERIMENTS.md` for the math.
