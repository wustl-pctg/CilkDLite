#!/usr/bin/env bash
# Portable replacement: options are documented by --help.
set -euo pipefail
suite_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
exec "$suite_dir/run.sh" --benchmarks fft "$@"
