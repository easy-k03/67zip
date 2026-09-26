#!/bin/sh
# Shared build for every host that has a C++17 compiler, POSIX make, zlib
# and liblzma. Platform scripts in this directory set the environment and
# then exec this file. They must not grow their own compile rules.
#
#   CXX          C++ compiler (c++, g++, clang++)
#   PREFIX       install prefix (default /usr/local; Nix passes $out)
#   PKG_CONFIG   pkg-config binary, or "false" to skip it
#   EXTRA_CXXFLAGS  EXTRA_CPPFLAGS  EXTRA_LDFLAGS  EXTRA_LDLIBS
#   JOBS         make -jN (default: 1, safe on small hosts)
#   SKIP_TEST    set to 1 to skip ./port_test (cross builds)
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$root"

: "${CXX:=c++}"
: "${PREFIX:=/usr/local}"
: "${JOBS:=1}"
: "${EXTRA_CXXFLAGS:=}"
: "${EXTRA_CPPFLAGS:=}"
: "${EXTRA_LDFLAGS:=}"
: "${EXTRA_LDLIBS:=}"

# Solaris and illumos ship a non-GNU /usr/bin/make. Platform scripts export
# MAKE=gmake; everyone else keeps make.
: "${MAKE:=make}"

if ! command -v "$CXX" >/dev/null 2>&1; then
    echo "67zip: compiler not found: $CXX" >&2
    echo "67zip: install a C++17 compiler and re-run, or set CXX=" >&2
    exit 127
fi

if [ -z "${PKG_CONFIG:-}" ]; then
    if command -v pkg-config >/dev/null 2>&1; then
        PKG_CONFIG=pkg-config
    else
        PKG_CONFIG=false
    fi
fi

echo "67zip: CXX=$CXX PREFIX=$PREFIX PKG_CONFIG=$PKG_CONFIG"

"$MAKE" -C "$root" -j"$JOBS" \
    CXX="$CXX" \
    PREFIX="$PREFIX" \
    PKG_CONFIG="$PKG_CONFIG" \
    CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -Wpedantic ${EXTRA_CXXFLAGS}" \
    CPPFLAGS="${EXTRA_CPPFLAGS}" \
    LDFLAGS="${EXTRA_LDFLAGS}" \
    LDLIBS="${EXTRA_LDLIBS}" \
    all

if [ "${SKIP_TEST:-0}" != 1 ]; then
    "$MAKE" -C "$root" \
        CXX="$CXX" \
        PKG_CONFIG="$PKG_CONFIG" \
        CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -Wpedantic ${EXTRA_CXXFLAGS}" \
        CPPFLAGS="${EXTRA_CPPFLAGS}" \
        LDFLAGS="${EXTRA_LDFLAGS}" \
        LDLIBS="${EXTRA_LDLIBS}" \
        test
fi

if [ "${DO_INSTALL:-0}" = 1 ]; then
    "$MAKE" -C "$root" install PREFIX="$PREFIX"
fi

echo "67zip: built $root/67zip"
