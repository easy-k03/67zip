#!/bin/sh
# illumos: OmniOS, OpenIndiana, SmartOS, Tribblix.
# OmniOS:        pkg install gcc pkg-config xz
# OpenIndiana:   pkg install developer/gcc developer/pkg-config compress/xz
# SmartOS:       pkgin install gcc pkg-config xz
# Use gmake, not /usr/bin/make.
set -eu
cd "$(dirname "$0")"
: "${CXX:=g++}"
export CXX
if command -v gmake >/dev/null 2>&1; then
    export MAKE=gmake
fi
exec ./build-posix.sh "$@"
