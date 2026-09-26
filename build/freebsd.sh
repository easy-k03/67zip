#!/bin/sh
# FreeBSD. pkg install gmake gcc pkgconf zlib liblzma
# Base clang is enough: CXX=c++ works when xz is installed from ports/pkg.
set -eu
cd "$(dirname "$0")"
exec ./build-posix.sh "$@"
