#!/bin/sh
# Solaris 11 / Oracle Solaris.
# pkg install developer/gcc developer/pkg-config compress/xz library/zlib
# The default cc is not a C++17 compiler; use g++ from the Solaris package.
set -eu
cd "$(dirname "$0")"
: "${CXX:=g++}"
export CXX
# /usr/bin/make is not GNU make. Prefer gmake when both exist.
if command -v gmake >/dev/null 2>&1; then
    alias make=gmake 2>/dev/null || true
    export MAKE=gmake
fi
exec ./build-posix.sh "$@"
