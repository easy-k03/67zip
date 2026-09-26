#!/bin/sh
# ReactOS is the Win32 port. RosBE's gcc is a mingw-w64 gcc; point CXX at it.
#   CXX=i686-w64-mingw32-g++ ./build/reactos.sh
set -eu
cd "$(dirname "$0")"
exec ./windows.sh "$@"
