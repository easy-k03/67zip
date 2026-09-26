#!/bin/sh
# Build 67zip and pack a binary tarball, or a FreeBSD pkg.
# Usage: packaging/stage.sh <kind>
#   kind: tarball | freebsd | macos | windows | android
# Runs on the target system (native runner or vmactions VM).
set -eu

kind=${1:-tarball}
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$root"

version=$(git describe --tags --always 2>/dev/null || echo 0.0.0)
version=${version#v}
arch=$(uname -m)
os=$(uname -s | tr '[:upper:]' '[:lower:]')

if [ "$kind" = "windows" ]; then
    : "${CXX:=x86_64-w64-mingw32-g++}"
    "$CXX" -std=c++17 -O2 -Wall -Wextra -o 67zip.exe src/main.cpp src/port.cpp -lz -llzma
    mkdir -p dist
    name="67zip-${version}-windows-x64.zip"
    if command -v zip >/dev/null 2>&1; then
        zip -j "dist/$name" 67zip.exe README.md
    else
        tar -caf "dist/67zip-${version}-windows-x64.tar.gz" 67zip.exe README.md
    fi
    exit 0
fi

make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"
./port_test

stage=$(mktemp -d)
mkdir -p "$stage/usr/bin" "$stage/usr/share/man/man1"
install -m 755 67zip "$stage/usr/bin/67zip"
install -m 644 packaging/67zip.1 "$stage/usr/share/man/man1/67zip.1"
cp README.md "$stage/README.md"

mkdir -p dist
name="67zip-${version}-${os}-${arch}"

if [ "$kind" = "freebsd" ] && command -v pkg >/dev/null 2>&1; then
    sh packaging/freebsd-pkg.sh "$stage" "$version" "$arch"
    rm -rf "$stage"
    exit 0
fi

tar -C "$stage" -czf "dist/${name}.tar.gz" .
rm -rf "$stage"
echo "67zip: packed dist/${name}.tar.gz"
