# PowerPC (Espresso) homebrew tests

Two small Wii U homebrew programs that exercise Cemu's PowerPC emulation
without needing any game:

- **`ppc_tests.rpx`**: deterministic instruction tests. Every tested instruction
  is emitted with inline asm and run on fixed and pseudo-random inputs. Covers
  integer arithmetic with XER/CR effects (including the `o` and `.` forms,
  divide by zero and `INT_MIN / -1`), logical ops, rotates/masks, shifts with
  amounts >= 32, compares, CR logical ops, `mtcrf`/`mfcr`/`mcrxr`, loads and
  stores (update, indexed, byte-reversed, `lmw`/`stmw`, `lswi`/`stswi`/`lswx`/`stswx`,
  `dcbz`), `lwarx`/`stwcx.`, FP arithmetic (single/double, fused multiply-add,
  `fres`/`frsqrte`, `fctiw[z]`, `frsp`, `fsel`, `fabs`/`fnabs`/`fneg`,
  `fcmpu`/`fcmpo`, NaN/infinity/denormal inputs, rounding modes via `mtfsf`,
  FPSCR via `mffs`), FP load/store conversions and paired singles (arithmetic,
  merges, sums, estimates, compares, `psq_l`/`psq_st` with several GQR types and
  scales, and how scalar FP instructions affect ps1).
- **`ppc_bench.rpx`**: CPU-bound microbenchmarks with a fixed amount of work:
  tight integer loop, branchy code, double precision math, paired single math,
  memcpy-style loads/stores, deep and indirect call chains (function-to-function
  transitions in the recompiler) and an `lwarx`/`stwcx.` loop.

The main use is validating recompiler changes (for example the AArch64 backend
in `src/Cafe/HW/Espresso/Recompiler/BackendAArch64`): the same program runs once
with the recompiler and once with `--force-interpreter`, and every result line
must be identical. The interpreter is the reference. Note that it hasn't been
verified against real hardware, so a test that matches is "consistent", not
necessarily "correct".

## Running

Prebuilt binaries are in `tests/ppc/prebuilt/`, so devkitPro isn't needed to
run the tests. On a Mac with a Cemu build:

```sh
scripts/run-ppc-tests.sh                       # uses bin/Cemu_release.app
scripts/run-ppc-tests.sh /Applications/Cemu.app
scripts/run-ppc-tests.sh --tests-only path/to/Cemu.app
scripts/run-ppc-tests.sh --no-interpreter-bench   # skip the slow interpreter benchmark run
```

The script copies the Cemu app into a temporary directory (an APFS clone, so it
is instant and uses no extra space) and runs it in portable mode with a
pre-made `settings.xml`, so your real settings in
`~/Library/Application Support/Cemu` are never read or modified and no
first-start dialog appears. For each run it waits for the `PPC_TESTS_DONE` /
`PPC_BENCH_DONE` marker, terminates Cemu and compares the results. Exit status
is 0 when everything matches, 1 on a recompiler/interpreter mismatch and 2 when
a run failed (timeout, crash). Logs, raw output and the mismatch report are
kept in the printed results directory (`--out DIR` to choose it).

Running a program manually:

```sh
Cemu -g tests/ppc/prebuilt/ppc_tests.rpx --forward-console-logging [--force-interpreter]
```

### How the output reaches the host

The programs print with coreinit's `OSConsoleWrite`. Cemu handles it in
`WriteCafeConsole` (`src/Cafe/OS/libs/coreinit/coreinit_Misc.cpp`):

- with the `--forward-console-logging` command line option it is written to
  **stdout** (this is what the script uses), and
- with *Debug > Logging > Coreinit Logging (OSReport/OSConsole)* enabled
  (`logflag` bit 17 in `settings.xml`) each line also goes to `log.txt` with an
  `[OSConsole]` prefix. The script enables this too and falls back to `log.txt`
  if stdout has no results.

Lines are kept below 256 characters because `log.txt` splits console lines at
270 characters.

## Reading the results

`ppc_tests` prints one line per test, `name: <hex words>`. Tests with many
results are split into `name.0`, `name.1`, ... (24 words per line). Test names
are the instruction mnemonics (`addco.`, `rlwinm,4,0,27`, `ps_madds0`, ...);
`fuzz.<insn>` lines are a hash over 256-512 pseudo-random inputs. The last
lines are `checksum: <hash of all result words>` and `PPC_TESTS_DONE`.

Cemu recompiles functions asynchronously after their first execution, so every
test runs 6 times with short sleeps in between and only the last pass is
printed (by then it runs as recompiled code). A `# unstable: <name>` line means
that test produced different results in different passes, which within the
recompiler run usually means interpreter and recompiler disagree.

Result words per test family:

- integer ops: pairs of `result, flags` where flags is `XER[SO,OV,CA]` in the
  top three bits and CR0 in the low nibble. Every input pair is run with XER
  clear and again with SO and CA set (carry-in and sticky SO).
- compares and FP compares: the CR field of each compare as one nibble, packed
  eight per word. The FP/paired compare tests end with a word holding any bits
  that changed in other CR fields (expected 0).
- FP ops: full 64-bit FPR contents (two words). `fctiw`/`fctiwz` print the
  integer (low) word; their high word is in the separate `fctiw.hi` test since
  it is undefined on some PowerPC implementations.
- paired singles: ps0 and ps1 as 64-bit doubles (ps1 read via `ps_merge10`).
- memory tests: loaded values, updated base offsets and dumps of the buffer.

Known gaps in Cemu that the tests work around rather than test: the interpreter
has no `mtfsfi`, `mtfsb0` or `mcrfs` (they would trigger an assert in debug
builds), and `mtfsf`
stores FPSCR without changing the host rounding mode. A mismatch in
`rounding_modes`, `mtfsf_mffs` or `fpscr_flags` is therefore about FPSCR
bookkeeping rather than arithmetic.

`ppc_bench` prints `bench <name>: us=<guest microseconds> checksum=<hex>` per
benchmark (time from `OSGetSystemTime`, which Cemu derives from the host
clock), then `bench_total` and `PPC_BENCH_DONE`. The script shows a table with
recompiler and interpreter times, the speedup and the host wall time of each
run. Checksums must match between the two runs.

## Building

Requires devkitPro with devkitPPC, wut and wut-tools. Either install them
(https://devkitpro.org/wiki/Getting_Started, then `dkp-pacman -S wiiu-dev`) and
run

```sh
tests/ppc/build.sh
```

or use the official docker image:

```sh
docker run --rm -v "$PWD":/src -w /src devkitpro/devkitppc tests/ppc/build.sh
```

The outputs are `tests/ppc/build/ppc_tests.rpx` and `ppc_bench.rpx`; the script
prefers them over the prebuilt copies when they exist. The
`Build PPC homebrew tests` workflow (`.github/workflows/homebrew_tests.yml`)
builds them on every change under `tests/ppc/` and uploads them as the
`ppc-homebrew-tests` artifact. After changing the sources, copy the new files
into `tests/ppc/prebuilt/`.

## Adding tests

Tests are table driven (`source/tests_*.c`): add a wrapper that executes the
instruction with inline asm, then an entry in the file's `TestDef` table. Keep
each wrapper small and `NOINLINE` so an instruction the recompiler can't handle
only keeps that one function in the interpreter. Results must be deterministic:
never print addresses or times from `ppc_tests`.
