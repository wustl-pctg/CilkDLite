#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"
require_linux_x86_64
[[ -x "$COMPILER_BIN/clang" && -x "$COMPILER_BIN/llvm-config" ]] || { printf 'Build the compiler first: scripts/build.sh compiler\n' >&2; exit 1; }
exec make -C "$PROJECT_ROOT/src/cheetah/runtime" -j "$JOBS" all
