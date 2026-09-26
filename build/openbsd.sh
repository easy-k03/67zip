#!/bin/sh
# OpenBSD. pkg_add gmake pkgconf xz
# Base clang++ is eg++ or c++; pick the one that speaks C++17.
set -eu
cd "$(dirname "$0")"
if [ -z "${CXX:-}" ]; then
    if command -v eg++ >/dev/null 2>&1; then
        CXX=eg++
    else
        CXX=c++
    fi
    export CXX
fi
exec ./build-posix.sh "$@"
