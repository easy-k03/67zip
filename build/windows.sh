#!/bin/sh
# Windows and ReactOS cross build (mingw-w64) or native MinGW.
# ReactOS uses the same ZIP67_OS_WINDOWS branch; pass CXX if its gcc is not
# the default mingw triplet.
#
#   apt install g++-mingw-w64-x86-64
#   # zlib and liblzma built for mingw, then:
#   LZMA_PREFIX=/opt/mingw ./build/windows.sh
set -eu
cd "$(dirname "$0")/.."
if [ -z "${CXX:-}" ]; then
    if command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
        CXX=x86_64-w64-mingw32-g++
    elif command -v i686-w64-mingw32-g++ >/dev/null 2>&1; then
        CXX=i686-w64-mingw32-g++
    else
        echo "67zip: no Win32 cross compiler on PATH." >&2
        echo "67zip: apt install g++-mingw-w64-x86-64, or set CXX= to a mingw g++." >&2
        echo "67zip: a native g++ is not used here; it would not produce a Windows binary." >&2
        exit 127
    fi
fi
flags=
if [ -n "${LZMA_PREFIX:-}" ]; then
    flags="$flags -I${LZMA_PREFIX}/include -L${LZMA_PREFIX}/lib"
fi
if [ -n "${ZLIB_PREFIX:-}" ]; then
    flags="$flags -I${ZLIB_PREFIX}/include -L${ZLIB_PREFIX}/lib"
fi
# shellcheck disable=SC2086
"$CXX" -std=c++17 -O2 -Wall -Wextra -o 67zip.exe src/main.cpp src/port.cpp $flags -lz -llzma
echo "67zip: built $(pwd)/67zip.exe"
