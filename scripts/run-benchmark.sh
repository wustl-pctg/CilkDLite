#!/usr/bin/env bash
# Use the frame-pointer libraries only for the benchmark, never for build tools.
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"
if [[ $# -eq 0 || ${1:-} == --help ]]; then
    printf 'Usage: %s EXECUTABLE [ARG ...]\n' "$0"
    printf 'CILK_NWORKERS controls workers. ALLOCATOR=system|tcmalloc (default: system).\n'
    exit 0
fi
require_linux_x86_64
ulimit -c 0
program=$1
shift
[[ -x "$program" ]] || { printf 'Benchmark is missing or not executable: %s\n' "$program" >&2; exit 1; }
allocator=${ALLOCATOR:-system}
case "$allocator" in
    system|tcmalloc) ;;
    *) printf 'Unknown ALLOCATOR: %s\n' "$allocator" >&2; exit 2;;
esac
require_commands readelf
dynamic_info=$(readelf -d "$program")
if [[ $allocator == tcmalloc || $dynamic_info == *libstdc++.so* ]] && \
   [[ ! -f "$DEPS_PREFIX/libstdcxx/lib64/libstdc++.so.6" && ! -f "$DEPS_PREFIX/libstdcxx/lib/libstdc++.so.6" ]]; then
    printf 'Frame-pointer libstdc++ is missing; run scripts/build-deps.sh libstdcxx.\n' >&2
    exit 1
fi
if [[ $allocator == tcmalloc || $dynamic_info == *libstdc++.so* || $dynamic_info == *libgcc_s.so* ]] && \
   [[ ! -f "$DEPS_PREFIX/libstdcxx/lib64/libgcc_s.so.1" && ! -f "$DEPS_PREFIX/libstdcxx/lib/libgcc_s.so.1" ]]; then
    printf 'Frame-pointer libgcc is missing; run scripts/build-deps.sh libstdcxx.\n' >&2
    exit 1
fi
loader="$DEPS_PREFIX/glibc/lib/ld-linux-x86-64.so.2"
[[ -x "$loader" ]] || { printf 'Frame-pointer glibc is missing; run scripts/build.sh deps.\n' >&2; exit 1; }
libpath="${CHEETAH_BUILD:-$BUILD_ROOT/cheetah}/lib/x86_64-unknown-linux-gnu:$DEPS_PREFIX/libstdcxx/lib64:$DEPS_PREFIX/libstdcxx/lib:$DEPS_PREFIX/gperftools/lib:$DEPS_PREFIX/glibc/lib:/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu:/lib64:/usr/lib64:/lib:/usr/lib"
case "$allocator" in
    system) exec "$loader" --library-path "$libpath" "$program" "$@";;
    tcmalloc)
        malloc_lib="$DEPS_PREFIX/gperftools/lib/libtcmalloc_minimal.so"
        [[ -f "$malloc_lib" ]] || { printf 'tcmalloc is missing; run scripts/build-deps.sh gperftools.\n' >&2; exit 1; }
        exec "$loader" --library-path "$libpath" --preload "$malloc_lib" "$program" "$@"
        ;;
esac
