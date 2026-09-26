#!/bin/sh
# FreeDOS via DJGPP. The port compiles (ZIP67_OS_FREEDOS): no threads, no
# symlinks, C file API. DJGPP's g++ is the compiler. zlib and liblzma must
# be built with the same toolchain.
#
#   export CXX=i586-pc-msdosdjgpp-g++
#   export LZMA_PREFIX=/opt/djgpp
#   ./build/freedos.sh
set -eu
cd "$(dirname "$0")/.."
: "${CXX:=i586-pc-msdosdjgpp-g++}"
if ! command -v "$CXX" >/dev/null 2>&1; then
    echo "67zip: DJGPP compiler not found: $CXX" >&2
    echo "67zip: install djgpp g++ and set CXX= to it." >&2
    exit 127
fi
flags="-DZIP67_OS_FREEDOS=1 -DZIP67_NO_THREADS=1"
if [ -n "${LZMA_PREFIX:-}" ]; then
    flags="$flags -I${LZMA_PREFIX}/include -L${LZMA_PREFIX}/lib"
fi
# shellcheck disable=SC2086
"$CXX" -std=c++17 -O2 -Wall -Wextra $flags -o 67zip.exe src/main.cpp src/port.cpp -lz -llzma
echo "67zip: built $(pwd)/67zip.exe"
