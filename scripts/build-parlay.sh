#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"
require_linux_x86_64
exec make -C "$PROJECT_ROOT/src/cheetah/parlaylib" -j "$JOBS" all "$@"
