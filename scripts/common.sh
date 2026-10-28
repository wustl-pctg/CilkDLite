#!/usr/bin/env bash
# Shared configuration. Source this from Bash; callers own their command line.
set -euo pipefail
PROJECT_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
BUILD_ROOT=${BUILD_ROOT:-"$PROJECT_ROOT/build"}
DEPS_PREFIX=${DEPS_PREFIX:-"$BUILD_ROOT/deps"}
COMPILER_BIN=${COMPILER_BIN:-"$BUILD_ROOT/opencilk/bin"}
PARLAY_ROOT=${PARLAY_ROOT:-"$PROJECT_ROOT/src/parlaylib"}
if [[ -z ${JOBS+x} ]]; then
    JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1')
    if [[ $JOBS -gt 8 ]]; then JOBS=8; fi
fi
if [[ ! $JOBS =~ ^[1-9][0-9]*$ ]]; then
    printf 'JOBS must be a positive integer (got %s).\n' "$JOBS" >&2
    exit 2
fi
export PROJECT_ROOT BUILD_ROOT DEPS_PREFIX COMPILER_BIN PARLAY_ROOT JOBS

require_linux_x86_64() {
    if [[ $(uname -s) != Linux || $(uname -m) != x86_64 ]]; then
        printf 'This backtracking runtime requires Linux x86-64. Use the linux/amd64 container.\n' >&2
        exit 2
    fi
    local setting value
    for setting in BUILD_ROOT DEPS_PREFIX COMPILER_BIN PARLAY_ROOT; do
        value=${!setting}
        if [[ $value != /* || $value == *[[:space:]]* ]]; then
            printf '%s must be an absolute path without whitespace inside the build environment.\n' "$setting" >&2
            exit 2
        fi
    done
    if [[ $PROJECT_ROOT == *[[:space:]]* || $BUILD_ROOT == *[[:space:]]* ]]; then
        printf 'Native GNU Make builds require paths without spaces. Use launch-container.sh (mounts at /workspace).\n' >&2
        exit 2
    fi
}

require_commands() {
    local command
    for command in "$@"; do
        command -v "$command" >/dev/null || { printf 'Missing required tool: %s\n' "$command" >&2; exit 2; }
    done
}
