#!/usr/bin/env bash
# Rebuild the libraries needed when backtracking walks frames in library code.
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"
stage=${1:-all}
case "$stage" in
    -h|--help) printf 'Usage: %s [all|glibc|libstdcxx|gperftools]\n' "$0"; exit 0;;
    all|glibc|libstdcxx|gperftools) ;;
    *) printf 'Unknown dependency: %s\n' "$stage" >&2; exit 2;;
esac
[[ $# -le 1 ]] || { printf 'Expected at most one dependency name.\n' >&2; exit 2; }
require_linux_x86_64
require_commands curl tar sha256sum sha512sum gcc g++ make gawk bison makeinfo patch
download_dir="$BUILD_ROOT/downloads"
source_dir="$BUILD_ROOT/dependency-sources"
build_dir="$BUILD_ROOT/dependency-builds"
mkdir -p "$download_dir" "$source_dir" "$build_dir" "$DEPS_PREFIX"
flags='-O2 -g -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer'

unpack() {
    local name=$1 url=$2 algorithm=$3 digest=$4 archive="$download_dir/${2##*/}"
    if [[ ! -f "$archive" ]]; then
        curl --fail --location --retry 3 --connect-timeout 30 --output "$archive.part" "$url"
        printf '%s  %s\n' "$digest" "$archive.part" | "${algorithm}sum" --check --status
        mv "$archive.part" "$archive"
    fi
    printf '%s  %s\n' "$digest" "$archive" | "${algorithm}sum" --check --status || {
        printf 'Checksum mismatch: %s. Remove that download and retry.\n' "$archive" >&2; exit 1;
    }
    if [[ ! -f "$source_dir/$name/.unpacked" ]]; then
        [[ ! -e "$source_dir/$name" ]] || { printf 'Incomplete extraction: %s\n' "$source_dir/$name" >&2; exit 1; }
        local temporary
        temporary=$(mktemp -d "$source_dir/.extract.XXXXXXXX")
        tar -xf "$archive" -C "$temporary" --strip-components=1
        printf '%s\n' "$digest" > "$temporary/.unpacked"
        mv "$temporary" "$source_dir/$name"
    fi
}

recipe=$( {
    sha256sum "$PROJECT_ROOT/scripts/build-deps.sh" "$PROJECT_ROOT/sources.json"
    command -v gcc g++
    gcc -dumpfullversion -dumpversion
    g++ -dumpfullversion -dumpversion
    printf '%s\n' "$PROJECT_ROOT" "$DEPS_PREFIX" "$flags"
} | sha256sum | cut -d' ' -f1 )
complete() {
    local name=$1 artifact=$2
    [[ -f "$artifact" && -f "$DEPS_PREFIX/$name/.build-recipe" ]] && [[ $(cat "$DEPS_PREFIX/$name/.build-recipe") == "$recipe" ]]
}
stamp() { printf '%s\n' "$recipe" > "$DEPS_PREFIX/$1/.build-recipe"; }

if [[ $stage == all || $stage == glibc ]]; then
    if complete glibc "$DEPS_PREFIX/glibc/lib/ld-linux-x86-64.so.2"; then
        printf 'Frame-pointer glibc is up to date.\n'
    else
        unpack glibc-2.35 https://ftp.gnu.org/gnu/libc/glibc-2.35.tar.gz sha256 3e8e0c6195da8dfbd31d77c56fb8d99576fb855fafd47a9e0a895e51fd5942d4
        mkdir -p "$build_dir/glibc"
        (
            cd "$build_dir/glibc"
            "$source_dir/glibc-2.35/configure" --prefix="$DEPS_PREFIX/glibc" --disable-werror \
                --disable-multi-arch --disable-nscd CC=gcc CXX=g++ CFLAGS="$flags"
            make -j "$JOBS"
            make install
        )
        stamp glibc
    fi
fi

if [[ $stage == all || $stage == libstdcxx ]]; then
    if complete libstdcxx "$DEPS_PREFIX/libstdcxx/lib64/libstdc++.so.6"; then
        printf 'Frame-pointer libstdc++ is up to date.\n'
    else
        # GCC publishes this SHA-512 at gcc.gnu.org/pub/gcc/releases/gcc-13.2.0/sha512.sum.
        unpack gcc-13.2.0 https://ftp.gnu.org/gnu/gcc/gcc-13.2.0/gcc-13.2.0.tar.xz sha512 d99e4826a70db04504467e349e9fbaedaa5870766cda7c5cab50cdebedc4be755ebca5b789e1232a34a20be1a0b60097de9280efe47bdb71c73251e30b0862a2
        mkdir -p "$build_dir/gcc"
        (
            cd "$build_dir/gcc"
            "$source_dir/gcc-13.2.0/configure" --prefix="$DEPS_PREFIX/libstdcxx" \
                --disable-multilib --disable-bootstrap --disable-nls --enable-languages=c,c++ \
                --disable-libsanitizer --disable-libquadmath --disable-libssp --disable-libgomp \
                CC=gcc CXX=g++ CFLAGS="$flags" CXXFLAGS="$flags" CFLAGS_FOR_TARGET="$flags" CXXFLAGS_FOR_TARGET="$flags"
            make -j "$JOBS" all-gcc all-target-libgcc all-target-libstdc++-v3
            make install-target-libgcc install-target-libstdc++-v3
        )
        stamp libstdcxx
    fi
fi

if [[ $stage == all || $stage == gperftools ]]; then
    # Hash the clean source tree independently of timestamps and ownership. Git
    # metadata is neither part of the fingerprint nor copied into generated sources.
    gperftools_archive() {
        tar --format=gnu --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner \
            --exclude=.git -C "$PROJECT_ROOT/src/gperftools" -cf - .
    }
    gperftools_digest=$(gperftools_archive | sha256sum | cut -d' ' -f1)
    recipe=$(printf '%s\n' "$recipe" "$gperftools_digest" | sha256sum | cut -d' ' -f1)
    gperftools_source="$source_dir/gperftools-$gperftools_digest"
    gperftools_build="$build_dir/gperftools-$recipe"
    if complete gperftools "$DEPS_PREFIX/gperftools/lib/libtcmalloc_minimal.so"; then
        printf 'Frame-pointer tcmalloc is up to date.\n'
    else
        require_commands autoreconf automake libtoolize
        if [[ ! -f "$gperftools_source/.source-digest" ]]; then
            [[ ! -e "$gperftools_source" ]] || { printf 'Incomplete source copy: %s\n' "$gperftools_source" >&2; exit 1; }
            temporary=$(mktemp -d "$source_dir/.gperftools.XXXXXXXX")
            gperftools_archive | tar --no-same-owner -xf - -C "$temporary"
            printf '%s\n' "$gperftools_digest" > "$temporary/.source-digest"
            mv "$temporary" "$gperftools_source"
        fi
        (cd "$gperftools_source" && autoreconf -fi)
        mkdir -p "$gperftools_build"
        (
            cd "$gperftools_build"
            "$gperftools_source/configure" --prefix="$DEPS_PREFIX/gperftools" \
                --disable-cpu-profiler --disable-heap-profiler --disable-heap-checker \
                CC=gcc CXX=g++ CFLAGS="$flags" CXXFLAGS="$flags"
            make -j "$JOBS"
            make install
        )
        stamp gperftools
    fi
fi
