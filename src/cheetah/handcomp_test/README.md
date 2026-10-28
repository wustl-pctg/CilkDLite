# Hand-translated runtime experiments

This suite preserves the manually translated x86-64 research drivers. Its
Makefile builds `fib` and `mm_dac` without compiler Tapir lowering; both are
included in the release build and have small-input checks for one and two workers. These are separate from the compiler-generated equivalents in
`cilkbench` and `compiled_test`.

```sh
make -C src/cheetah/handcomp_test
src/cheetah/handcomp_test/run.sh --smoke --workers 1,2
```

The remaining sources require further porting and are preserved rather than
silently substituted with different algorithms:

- `nqueens.c` calls `nqueens_zero`, whose implementation is commented out, and
  registers assembly labels from that missing implementation.
- `mm_dac_serial.c` uses the removed `oldest_frame_rbp` closure member.
- `cilksort.c` and the C++/loop experiments lack the current runtime entry or
  spawn-registration integration.
- `fib_serial.c` is a research variant, not a portable serial baseline.

Old runner filenames accept the shared named options. Runners for unported
programs report that the target is unavailable. Build products are kept under
`build/benchmarks/handcomp_test`; new run logs go under `build/results`.
The legacy plotting scripts require separately supplied historical datasets.
