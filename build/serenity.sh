#!/bin/sh
# SerenityOS. symlink and termios are not assumed. The build defines
# ZIP67_OS_SERENITY, which selects the C file API in src/port.cpp.
#
#   CXX=x86_64-pc-serenity-g++ LZMA_PREFIX=/opt/serenity ./build/serenity.sh
set -eu
cd "$(dirname "$0")/.."
: "${CXX:=x86_64-pc-serenity-g++}"
if ! command -v "$CXX" >/dev/null 2>&1; then
    echo "67zip: Serenity compiler not found: $CXX" >&2
    echo "67zip: build from a Serenity/Lagom toolchain and set CXX=." >&2
    exit 127
fi
flags="-DZIP67_OS_SERENITY=1"
if [ -n "${LZMA_PREFIX:-}" ]; then
    flags="$flags -I${LZMA_PREFIX}/include -L${LZMA_PREFIX}/lib"
fi
# shellcheck disable=SC2086
"$CXX" -std=c++17 -O2 -Wall -Wextra $flags -o 67zip src/main.cpp src/port.cpp -lz -llzma
echo "67zip: built $(pwd)/67zip"
