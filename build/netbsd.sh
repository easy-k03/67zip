#!/bin/sh
# NetBSD. pkgin install gmake pkg-config zlib xz
# Use the toolchain's c++ (gcc or clang, depending on the release).
set -eu
cd "$(dirname "$0")"
exec ./build-posix.sh "$@"
