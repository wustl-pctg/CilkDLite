# Cilk benchmark drivers

Build with the release's `scripts/build.sh` first. This directory's Makefile
also supports `make -C src/cheetah/cilkbench` from the release root once the
compiler and runtime are built. Executables, objects, and generated LLVM IR
are placed under `build/benchmarks/cilkbench`.

The build targets are `fib`, `nqueens`, `cilksort`, `fft`, `cholesky`, `lu`,
`heat`, `strassen`, `matmul`, `rectmul`, and `cilk_for_test`. The old `qsort`
target referred to a source file absent from the selected branch and has been
removed. `BFS.cpp`, `backtrack.c`, `cilksort_vanilla.c`, `fibred.c`, and
`rectmulred.c` remain as research source; they are not part of the supported
build. The Parlay BFS port lives in the neighboring `parlaylib` directory.

```sh
src/cheetah/cilkbench/run.sh --smoke
src/cheetah/cilkbench/run.sh --benchmarks fft --workers 1,2,4 --size 65536 --repeats 3
python3 src/cheetah/cilkbench/run.py --suite all --list
```

The runner accepts `--output-dir`, `--timeout`, `--build-root`, and `--dry-run`.
`--size` applies to one selected benchmark to avoid accidentally using a matrix
size as a Fibonacci index. Repetitions launch separate processes. The default
inputs are small, and the default worker counts are 1 and 2; no CPU affinity or
machine-wide environment changes are required. Outputs go into a unique
`build/results/<run>/` directory with a CSV summary, exact invocation settings,
and separate standard output/error logs. The CSV's wall time includes startup;
benchmark-internal timings remain in the logs. Fibonacci, loop, and small N-queens checks assert known results. Sorting and
matrix drivers check their built-in verification output. Graph checks validate
output size/ranges (labeled `sanity-passed`), and unverified drivers are labeled
`not-checked`; a zero exit code alone does not prove algorithm correctness.

`run_all_tests.sh` and the `*_timing_check.sh` entrypoints now accept the same
named options. Their old positional machine/runtime arguments and fixed
40-core sweeps have been retired. Use `BASE_CASE=...` and `TIMING_COUNT=...` as
Make options when needed; changed options trigger recompilation. The runner's
`--repeats` is usually preferable to compile-time timing repetitions.

The compiler pipeline emits Tapir IR, applies `loop-spawning,default<O3>`, then
lowers with the backtracking target and freshly built runtime ABI bitcode, then
runs `always-inline` to materialize the ABI helpers. Final
object compilation disables a second generic LLVM IR optimization pipeline,
which otherwise crashes on backtracking callbr IR, while retaining optimized
machine-code generation. The
small linker script augments GNU ld's native layout with the required spawn
metadata in an allocated, readable ELF section, preserving the existing
non-PIE executable model.

The `chart_*.py`, `add_offsets.py`, and `generate_tables.sh` analysis scripts are
retained for historical data formats; they do not consume the new summary CSV.
Run them from a result directory containing the legacy inputs. Some require
NumPy, Matplotlib, cycler, tikzplotlib, or webcolors. `chart_benchmarks.py` accepts
`--output-dir`; its default is `build/results/figures`. No historical raw results
or figures are included in this snapshot.
