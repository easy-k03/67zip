#!/bin/sh
# macOS. Needs the Xcode command-line tools, plus zlib (system) and liblzma.
#   brew install xz pkg-config
#   xcode-select --install
set -eu
cd "$(dirname "$0")"
: "${CXX:=clang++}"
export CXX
# System zlib has no pkg-config file on older releases; liblzma from Homebrew
# lives off the default search path. pkg-config covers that when present.
if [ -z "${EXTRA_CPPFLAGS:-}" ] && [ -d /opt/homebrew/include ]; then
    EXTRA_CPPFLAGS="-I/opt/homebrew/include"
    EXTRA_LDFLAGS="-L/opt/homebrew/lib"
    export EXTRA_CPPFLAGS EXTRA_LDFLAGS
elif [ -z "${EXTRA_CPPFLAGS:-}" ] && [ -d /usr/local/include ]; then
    EXTRA_CPPFLAGS="-I/usr/local/include"
    EXTRA_LDFLAGS="-L/usr/local/lib"
    export EXTRA_CPPFLAGS EXTRA_LDFLAGS
fi
exec ./build-posix.sh "$@"
