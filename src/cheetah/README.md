# Backtracking runtime and benchmarks

Use the [release instructions](../../README.md) to build the pinned compiler and
dependencies first. The supported build uses GNU Make on x86-64 Linux.

From this directory:

```sh
make -j2 runtime          # Runtime libraries, compiler ABI bitcode, and Cilk headers
make -j2 benchmarks       # Cilkbench and the supported Parlay programs
make -j2 compiled-tests   # Small compiler/runtime examples
make check                # Quick correctness checks
```

The default `make -j2` builds the runtime, benchmarks, and compiled tests.
Use a job count appropriate for the machine. Handwritten runtime experiments
are available separately through `make handcomp-tests`; see each suite's
documentation for the programs supported by this snapshot.

Configuration is shared in [config.mk](config.mk). Paths default to the release
layout and can be overridden on the command line:

```sh
make -j2 runtime COMPILER_BIN=/absolute/path/to/compiler/bin BUILD_ROOT=/absolute/path/to/build
```

`DEPS_PREFIX` selects the prepared dependency installations, and `PARLAY_ROOT`
selects the Parlay headers. Keep dependency preparation and program execution
in the release scripts so that the custom libc is selected only for the
programs that need it.

Runtime outputs are placed under `BUILD_ROOT/cheetah`:

- `obj/`: object files and dependency files.
- `lib/x86_64-unknown-linux-gnu/`: static/shared libraries and
  `libopencilk-abi.bc`.
- `include/cilk/`: copied public headers.

The compiler and benchmark Makefiles use this runtime's bitcode and linker
scripts for backtracking. See the benchmark build recipes for the complete
flags; a plain upstream `-fopencilk` invocation does not select this pipeline.

`make clean` removes generated runtime and benchmark products. Source inputs
and benchmark runners remain in the snapshot; generated measurements, figures,
LLVM IR, assembly dumps, and executables are excluded.

The inherited CMake files do not describe the backtracking runtime completely.
Use the Make targets above for this release.
