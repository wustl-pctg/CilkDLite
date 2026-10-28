# Compiler/runtime checks

The sources present in the selected branch build `fib` and `cilk_for_test`.
Former targets `mm_dac`, `nqueens`, and `cilksort` referred to missing sources
and have been removed from this Makefile.

After building the release, run:

```sh
src/cheetah/compiled_test/run.sh --smoke --workers 1,2
```

Both programs have known-result checks in the shared runner. `cilk_for_test`
now registers its compiler-generated spawn metadata and enters/leaves the
backtracking runtime, matching the other compiler drivers. It resets its array
between compile-time repetitions and returns failure on allocation or result
errors. Generated code and binaries live under `build/benchmarks/compiled_test`.

See `../cilkbench/README.md` for runner options and the explicit compiler
pipeline. The legacy plotting script remains for separately supplied old data.
