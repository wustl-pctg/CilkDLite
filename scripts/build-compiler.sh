#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"
require_linux_x86_64
require_commands cmake ninja clang clang++
cmake -S "$PROJECT_ROOT/src/opencilk/llvm" -B "$BUILD_ROOT/opencilk" -G Ninja \
    -DCMAKE_BUILD_TYPE="${LLVM_BUILD_TYPE:-Release}" \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DLLVM_ENABLE_PROJECTS=clang -DLLVM_ENABLE_RUNTIMES= \
    -DLLVM_TARGETS_TO_BUILD=X86 -DLLVM_ENABLE_ASSERTIONS="${LLVM_ASSERTIONS:-ON}" \
    -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF \
    -DLLVM_PARALLEL_LINK_JOBS="${LINK_JOBS:-2}" -DLLVM_OPTIMIZED_TABLEGEN=ON
cmake --build "$BUILD_ROOT/opencilk" --parallel "$JOBS" --target clang opt llvm-link llvm-config
