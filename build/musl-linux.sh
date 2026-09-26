#!/bin/sh
# musl Linux (Alpine, Void-musl, musl-cross).
# Alpine: apk add build-base zlib-dev xz-dev pkgconf
# The compiler is usually g++ from build-base; a cross toolchain sets CXX.
set -eu
cd "$(dirname "$0")"
: "${CXX:=g++}"
export CXX
exec ./build-posix.sh "$@"
