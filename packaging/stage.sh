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
case "$version" in
    *[!A-Za-z0-9._+]*) version=0.0.0 ;;
esac
arch=$(uname -m)
os=$(uname -s | tr '[:upper:]' '[:lower:]')

if [ "$kind" = "windows" ]; then
    : "${CXX:=x86_64-w64-mingw32-g++}"
    # MSYS2's mingw-w64 is ucrt and declares localtime_s, not localtime_r.
    # Headers live under /mingw64/include, which is not a default search
    # path for the cross-named driver.
    inc=""
    lib=""
    if [ -d /mingw64/include ]; then
        inc="-I/mingw64/include"
        lib="-L/mingw64/lib"
    fi
    "$CXX" -std=c++17 -O2 -Wall -Wextra $inc $lib -o 67zip.exe src/main.cpp src/port.cpp -lz -llzma
    mkdir -p dist
    name="67zip-${version}-windows-x64.zip"
    if command -v zip >/dev/null 2>&1; then
        zip -j "dist/$name" 67zip.exe README.md
    else
        tar -caf "dist/67zip-${version}-windows-x64.tar.gz" 67zip.exe README.md
    fi
    exit 0
fi

# Solaris and OmniOS install gcc under /usr/gcc/*/bin and /opt/gcc-*/bin,
# which are not on the default PATH of a non-login sh.
if ! command -v c++ >/dev/null 2>&1 && ! command -v g++ >/dev/null 2>&1; then
    for d in /usr/gcc/*/bin /opt/gcc-*/bin /opt/gcc*/bin; do
        if [ -x "$d/g++" ]; then
            PATH="$d:$PATH"
            export PATH
            break
        fi
    done
fi
# BSD make rejects -jN on some releases, and OpenBSD's make is not GNU
# make. A plain `make` is the portable call. illumos calls it gmake.
if command -v gmake >/dev/null 2>&1 && ! command -v make >/dev/null 2>&1; then
    gmake
    gmake test
else
    make
    make test
fi

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
