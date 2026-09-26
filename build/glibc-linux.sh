#!/bin/sh
# glibc Linux (Debian, Fedora, Ubuntu, RHEL, Arch, ...).
# Needs: g++ or clang++, zlib-dev, liblzma-dev / xz-devel, pkg-config.
set -eu
cd "$(dirname "$0")"
exec ./build-posix.sh "$@"
