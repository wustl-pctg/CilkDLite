#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
PROJECT_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd -P)
source "$SCRIPT_DIR/container.conf"

usage() {
    cat <<EOF
Usage: $0 [--debug] [--] [COMMAND [ARG...]]

Run COMMAND in /workspace, or open a Bash shell when no command is given.
The checkout is mounted at /workspace and files retain your host UID/GID.
--debug enables ptrace and disables Docker's seccomp filter for debugging.

Configuration: CONTAINER_NAME, CONTAINER_TAG, CONTAINER_PLATFORM, CONTAINER_DEBUG=0|1.
Examples:
  $0
  $0 ./scripts/initial-build.sh
  $0 env JOBS=8 ./scripts/initial-build.sh
EOF
}

while (($#)); do
    case $1 in
        --debug) CONTAINER_DEBUG=1; shift ;;
        --help|-h) usage; exit 0 ;;
        --) shift; break ;;
        -*) echo "Unknown launcher option: $1" >&2; usage >&2; exit 2 ;;
        *) break ;;
    esac
done

if [[ $CONTAINER_DEBUG != 0 && $CONTAINER_DEBUG != 1 ]]; then
    echo "CONTAINER_DEBUG must be 0 or 1." >&2
    exit 2
fi
if ! command -v docker >/dev/null 2>&1; then
    echo "Docker is required. Install Docker and start its daemon before launching." >&2
    exit 1
fi

HOST_UID=${SUDO_UID:-$(id -u)}
HOST_GID=${SUDO_GID:-$(id -g)}
docker_args=(run --rm --init --interactive --platform "$CONTAINER_PLATFORM"
    --mount "type=bind,source=$PROJECT_ROOT,target=/workspace"
    --workdir /workspace
    --env "HOST_UID=$HOST_UID" --env "HOST_GID=$HOST_GID")
if [[ -t 0 && -t 1 ]]; then
    docker_args+=(--tty)
fi
if [[ $CONTAINER_DEBUG == 1 ]]; then
    docker_args+=(--cap-add=SYS_PTRACE --security-opt seccomp=unconfined)
fi
if (($# == 0)); then
    set -- /bin/bash
fi

exec docker "${docker_args[@]}" "$CONTAINER_NAME:$CONTAINER_TAG" "$@"
