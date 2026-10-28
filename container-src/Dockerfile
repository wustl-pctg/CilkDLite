FROM ubuntu:22.04

# Build dependencies for OpenCilk, Cheetah, glibc, and GCC.  util-linux supplies
# setpriv, used by the entrypoint to run commands with the host user's IDs.
RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        autoconf \
        automake \
        bison \
        build-essential \
        ca-certificates \
        clang \
        cmake \
        curl \
        gawk \
        git \
        libgmp-dev \
        libmpc-dev \
        libmpfr-dev \
        libtool \
        libunwind-dev \
        ninja-build \
        passwd \
        patch \
        pkg-config \
        python3 \
        python3-venv \
        texinfo \
        util-linux \
        xz-utils \
        zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

COPY entrypoint.sh /usr/local/bin/opencilk-entrypoint
WORKDIR /workspace
ENTRYPOINT ["/usr/local/bin/opencilk-entrypoint"]
CMD ["/bin/bash"]
