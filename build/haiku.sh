#!/bin/sh
# Haiku. pkgman install gcc make zlib_devel xz_utils_devel pkgconfig
# The POSIX branch in src/port.cpp covers Haiku. No extra flags.
set -eu
cd "$(dirname "$0")"
: "${CXX:=g++}"
export CXX
exec ./build-posix.sh "$@"
