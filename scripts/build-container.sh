#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
PROJECT_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd -P)
source "$SCRIPT_DIR/container.conf"

if [[ ${1:-} == --help || ${1:-} == -h ]]; then
    echo "Usage: $0 [docker build options]"
    echo "Builds $CONTAINER_NAME:$CONTAINER_TAG for $CONTAINER_PLATFORM."
    exit 0
fi
if ! command -v docker >/dev/null 2>&1; then
    echo "Docker is required. Install Docker and start its daemon before building." >&2
    exit 1
fi

exec docker build --platform "$CONTAINER_PLATFORM" \
    --tag "$CONTAINER_NAME:$CONTAINER_TAG" "$@" "$PROJECT_ROOT/container-src"
