#!/bin/sh
# Redox OS. relibc does not implement the POSIX calls a stock build needs,
# so this script forces the constrained branch (C file API, no symlink, no
# termios). That branch is what src/port.cpp compiles under ZIP67_OS_REDOX.
#
#   CXX=x86_64-unknown-redox-g++ LZMA_PREFIX=/opt/redox ./build/redox.sh
set -eu
cd "$(dirname "$0")/.."
: "${CXX:=x86_64-unknown-redox-g++}"
if ! command -v "$CXX" >/dev/null 2>&1; then
    echo "67zip: Redox compiler not found: $CXX" >&2
    echo "67zip: install the Redox toolchain and set CXX=." >&2
    exit 127
fi
flags="-DZIP67_OS_REDOX=1"
if [ -n "${LZMA_PREFIX:-}" ]; then
    flags="$flags -I${LZMA_PREFIX}/include -L${LZMA_PREFIX}/lib"
fi
# shellcheck disable=SC2086
"$CXX" -std=c++17 -O2 -Wall -Wextra $flags -o 67zip src/main.cpp src/port.cpp -lz -llzma
echo "67zip: built $(pwd)/67zip"
