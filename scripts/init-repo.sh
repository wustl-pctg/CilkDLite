#!/usr/bin/env bash
# The public distribution contains source trees, not private submodules.
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"
for required in src/opencilk/llvm/CMakeLists.txt src/cheetah/runtime/Makefile src/cilktools/CMakeLists.txt src/parlaylib/include/parlay/parallel.h src/gperftools/configure.ac; do
    [[ -f "$PROJECT_ROOT/$required" ]] || { printf 'Missing snapshot source: %s\n' "$required" >&2; exit 1; }
done
printf 'Source snapshot is complete. No Git credentials or submodule initialization are needed.\n'
