# hammerblade-dp

This repository contains four applications:

- `1d`
- `dynamic`
- `2d`
- `chaining`

Use `make list-apps` to see the corresponding directories.

## Native Runs

Native runs use `hammer-sim` and work from a normal development machine.

Build and run all native tests:

```sh
make native
```

Run one application natively:

```sh
make native APP=2d
make native APP=chaining
```

Run one native test with full program output:

```sh
make native APP=2d TEST=seq-len_32__num-seq_8
make native APP=chaining TEST=chain-len_256__lookback_4
```

`make native` prints one parsed summary block per application:

```text
1d:
----------------------------------------------------------------------
seq-len_16__num-seq_64   PASSED
seq-len_32__num-seq_64   PASSED
```

## Simulator Runs

Simulator runs are only supported on `brg-rhel8`.

First load the HammerBlade toolchain:

```sh
module load hammerblade
```

From the repository root, run one simulator test with the wrapper make target:

```sh
make sim APP=2d TEST=seq-len_32__num-seq_8
make sim APP=chaining TEST=chain-len_256__lookback_4
```

You can also run the underlying per-application simulator flow directly. From an
app directory:

```sh
make generate
make -C <test-name> profile.log
```

Examples:

```sh
cd sw/2d
make generate
make -C seq-len_32__num-seq_8 profile.log
```

```sh
cd chaining
make generate
make -C chain-len_256__lookback_4 profile.log
```

The `chaining` app now has multiple variants under `chaining/`.

For the underlying per-application simulator flow, use the variant directory:

```sh
cd chaining/direct
make generate
make -C chain-len_256__lookback_4 profile.log
```

## Useful Commands

List available application names:

```sh
make list-apps
```

Clean generated native output and app test directories:

```sh
make clean
```
