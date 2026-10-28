# Release validation

Validated on Linux x86-64 in the included Ubuntu 22.04 Docker environment on
2026-10-25. Exact source revisions and dependency archive checksums are recorded
in [sources.json](sources.json).

## Build and execution checks

- The container image, selected OpenCilk compiler, cleaned Cheetah runtime,
  frame-pointer glibc 2.35, GCC 13.2.0 runtime libraries, and pinned gperftools
  built successfully.
- All 17 declared benchmark targets built inside the container. The complete
  documented `scripts/build.sh` command then succeeded, reusing the compiler,
  libraries, and benchmark products and running its 22 smoke cases.
- Runtime incremental checks preserved all 19 objects and ABI bitcode on an
  unchanged build. Changing debug flags rebuilt all 20 products. A simulated
  compiler timestamp change scheduled every product for rebuilding. All three
  shared libraries have their expected SONAMEs.
- Dependency reruns recognized all three completed installations. Launcher
  checks rejected missing local C++/libgcc libraries and invalid allocator names.
- Container checks covered a host checkout path containing spaces, host UID/GID
  ownership, argument boundaries, piped input, command exit status, an interactive
  shell entrypoint, and the optional debugging mode.

## Benchmark results

`python3 src/cheetah/cilkbench/run.py --suite all --workers 1,2` completed all
34 runs with exit status zero:

| Suite | Targets | Runs |
| --- | ---: | ---: |
| cilkbench | 11 | 22 |
| compiled_test | 2 | 4 |
| parlaylib | 2 | 4 |
| handcomp_test | 2 | 4 |

Of these runs, 24 passed known-result or driver verification checks. The four
BFS/PageRank runs passed graph dimension and output-range sanity checks. The
six FFT, Cholesky, and heat runs were completion checks; their numerical results
were not independently verified. Optional tcmalloc runs of Fibonacci and BFS
also passed at one and two workers.

These small-input checks establish build and basic execution coverage. They do
not establish performance, scaling, or correctness at all input sizes and worker
counts. Preserved drivers outside the declared targets still need porting; each
suite's README identifies them. The complete upstream LLVM regression suite
was not run. Native builds and other architectures were not release-tested.

## Source snapshot checks

- The explicit allowlist was checked against the selected repository trees and
  approved additions: 140,738 source files/links.
- Runtime cleanup removed 506 generated files totaling 211,820,405 bytes,
  including the accidentally tracked LLVM IR. No runtime or benchmark source
  files were removed. LLVM's 36,716 `.ll` regression inputs remain included.
- Compiler and cilktools source trees match their recorded commits. The runtime
  comes from `master`; other research branches and Git metadata are excluded.
- Thirteen snapshot-tool tests passed, covering metadata/artifact rejection,
  symlink safety, explicit additions, revision verification, deterministic
  inventories, and exporting again without Git metadata.
- Shell/Python syntax and Git whitespace checks passed.
- The exported source copy was mounted read-only for a fresh LLVM configuration
  and build plan, runtime and all 17 benchmark builds using the previously
  validated compiler/dependencies, 22 smoke runs, and the 13 snapshot tests.
  Generated products were written to a separate mounted build directory.
- Every exported source file was independently compared against its inventory
  and source content/mode; all checksum entries verified. A fresh-repository
  import check identified inherited ignore rules, so the README stages the
  explicit allowlist with `--force` to preserve all included source files.
  A staging dry run accepted all 140,738 source paths and both checksum metadata
  files using batches of 512, without creating Git metadata in the public copy.

Build logs and benchmark CSV/stdout/stderr records are generated under `build/`
and are intentionally excluded from the source snapshot. The exporter creates
`snapshot-inventory.json` and `SHA256SUMS` for the files it actually copies.
