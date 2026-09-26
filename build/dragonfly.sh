#!/bin/sh
# DragonFly BSD. pkg install gcc pkgconf xz
set -eu
cd "$(dirname "$0")"
exec ./build-posix.sh "$@"
