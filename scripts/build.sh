#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"
usage() {
    printf 'Usage: %s [all|compiler|deps|runtime|benchmarks|smoke]\n' "$0"
    printf 'Configure with JOBS, BUILD_ROOT, LLVM_BUILD_TYPE, LLVM_ASSERTIONS.\n'
}
stage=${1:-all}
[[ $# -le 1 ]] || { usage >&2; exit 2; }
case "$stage" in -h|--help) usage; exit 0;; all|compiler|deps|runtime|benchmarks|smoke) ;; *) usage >&2; exit 2;; esac
require_linux_x86_64
"$PROJECT_ROOT/scripts/init-repo.sh"
case "$stage" in all|compiler) "$PROJECT_ROOT/scripts/build-compiler.sh";; esac
case "$stage" in all|deps) "$PROJECT_ROOT/scripts/build-deps.sh";; esac
case "$stage" in all|runtime) "$PROJECT_ROOT/scripts/compile-cheetah.sh";; esac
case "$stage" in all|benchmarks)
    for suite in cilkbench parlaylib compiled_test handcomp_test; do
        make -C "$PROJECT_ROOT/src/cheetah/$suite" -j "$JOBS" all
    done
    ;;
esac
case "$stage" in all|smoke) python3 "$PROJECT_ROOT/src/cheetah/cilkbench/run.py" --suite all --smoke;; esac
