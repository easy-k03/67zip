#!/bin/sh
# GhostBSD. Same userland as FreeBSD; the package tool is pkg.
# pkg install gcc pkgconf zlib liblzma
# -DZIP67_OS_GHOSTBSD=1 only changes the name printed by `67zip i`.
set -eu
cd "$(dirname "$0")"
export EXTRA_CXXFLAGS="${EXTRA_CXXFLAGS:-} -DZIP67_OS_GHOSTBSD=1"
exec ./build-posix.sh "$@"
