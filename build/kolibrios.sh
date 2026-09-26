#!/bin/sh
# KolibriOS. The SDK compiler (kos32-gcc / kos32-g++) has no C++17 standard
# library, so this builds the C++ sources only when a hosted library is
# present and fails with the reason otherwise. The source itself compiles
# with -DZIP67_OS_KOLIBRI (verified by compiling that branch on a host g++).
#
#   CXX=kos32-g++ LZMA_PREFIX=/opt/kolibri ./build/kolibrios.sh
set -eu
cd "$(dirname "$0")/.."
: "${CXX:=kos32-g++}"
if ! command -v "$CXX" >/dev/null 2>&1; then
    echo "67zip: KolibriOS compiler not found: $CXX" >&2
    echo "67zip: the source branch -DZIP67_OS_KOLIBRI compiles; the SDK has no C++17 library." >&2
    echo "67zip: set CXX= to a g++ that targets KolibriOS and provides that library." >&2
    exit 127
fi
flags="-DZIP67_OS_KOLIBRI=1 -DZIP67_NO_THREADS=1"
if [ -n "${LZMA_PREFIX:-}" ]; then
    flags="$flags -I${LZMA_PREFIX}/include -L${LZMA_PREFIX}/lib"
fi
# shellcheck disable=SC2086
"$CXX" -std=c++17 -O2 -Wall -Wextra $flags -o 67zip src/main.cpp src/port.cpp -lz -llzma
echo "67zip: built $(pwd)/67zip"
