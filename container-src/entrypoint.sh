#!/usr/bin/env bash
set -euo pipefail

# The launcher supplies these IDs so files written to /workspace belong to the
# invoking host user.  Account records and the home directory are ephemeral.
HOST_UID=${HOST_UID:-$(id -u)}
HOST_GID=${HOST_GID:-$(id -g)}
if [[ ! $HOST_UID =~ ^[0-9]+$ || ! $HOST_GID =~ ^[0-9]+$ ]]; then
    echo "HOST_UID and HOST_GID must be numeric user and group IDs." >&2
    exit 2
fi

if (($# == 0)); then
    set -- /bin/bash
fi

if [[ $(id -u) == 0 ]]; then
    if ! getent group "$HOST_GID" >/dev/null; then
        groupadd --gid "$HOST_GID" opencilk
    fi
    if ! getent passwd "$HOST_UID" >/dev/null; then
        useradd --uid "$HOST_UID" --gid "$HOST_GID" \
            --home-dir /home/opencilk --no-create-home --shell /bin/bash opencilk
    fi
    export HOME=/home/opencilk
    export USER
    USER=$(id -nu "$HOST_UID")
    export LOGNAME=$USER
    mkdir -p "$HOME"
    cp -n /etc/skel/.bashrc "$HOME/.bashrc"
    chown "$HOST_UID:$HOST_GID" "$HOME" "$HOME/.bashrc"
    exec setpriv --reuid "$HOST_UID" --regid "$HOST_GID" --clear-groups -- "$@"
fi

# Also allow callers to use Docker's --user flag directly.
export HOME=/tmp/opencilk-home-$(id -u)
mkdir -p "$HOME"
exec "$@"
