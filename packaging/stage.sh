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
    # -llzma prefers liblzma.dll.a, so the exe looks for liblzma-5.dll at
    # runtime. Link the static archives and fold in libgcc/libstdc++ so the
    # zip runs on a machine that has neither MSYS2 nor those DLLs.
    libdir=""
    for d in /mingw64/lib /usr/x86_64-w64-mingw32/lib /usr/lib/gcc/x86_64-w64-mingw32/*/ ; do
        if [ -f "$d/liblzma.a" ] && [ -f "$d/libz.a" ]; then
            libdir=$d
            break
        fi
    done
    if [ -z "$libdir" ]; then
        echo "67zip: no static liblzma.a and libz.a for the Windows toolchain." >&2
        echo "67zip: refusing a DLL-linked exe." >&2
        exit 1
    fi
    "$CXX" -std=c++17 -O2 -Wall -Wextra $inc -static -o 67zip.exe \
        src/main.cpp src/port.cpp "$libdir/liblzma.a" "$libdir/libz.a"
    if command -v objdump >/dev/null 2>&1; then
        deps=$(objdump -p 67zip.exe | awk '/DLL Name/{print $3}')
        echo "$deps"
        echo "$deps" | grep -E 'liblzma|libz|libgcc|libstdc|libwinpthread' && {
            echo "67zip: 67zip.exe still imports a non-system DLL." >&2
            exit 1
        }
    fi
    mkdir -p dist
    name="67zip-${version}-windows-x64.zip"
    if command -v zip >/dev/null 2>&1; then
        zip -j "dist/$name" 67zip.exe README.md
    else
        tar -caf "dist/67zip-${version}-windows-x64.tar.gz" 67zip.exe README.md
    fi
    exit 0
fi

# Solaris make and illumos make reject '?='. BSD make rejects '$(shell)'.
# Do not call make here. Find a C++17 compiler and the two libraries, then
# compile the two translation units directly.
if [ -z "${CXX:-}" ]; then
    for d in /usr/gcc/*/bin /opt/gcc-*/bin /opt/gcc*/bin /usr/bin /usr/local/bin /opt/local/bin; do
        [ -d "$d" ] || continue
        if [ -x "$d/g++" ]; then PATH="$d:$PATH"; break; fi
        if [ -x "$d/clang++" ]; then PATH="$d:$PATH"; break; fi
    done
    export PATH
    if command -v g++ >/dev/null 2>&1; then CXX=g++
    elif command -v clang++ >/dev/null 2>&1; then CXX=clang++
    elif command -v c++ >/dev/null 2>&1; then CXX=c++
    else
        echo "67zip: no C++ compiler on PATH" >&2
        exit 1
    fi
fi

cflags="-std=c++17 -O2 -Wall -Wextra -pthread ${CPPFLAGS:-}"
ldflags="-pthread ${LDFLAGS:-} -lz -llzma"
# OpenBSD keeps lzma.h in /usr/local/include. NetBSD keeps it in /usr/pkg.
for inc in /usr/local/include /usr/pkg/include /opt/local/include /opt/homebrew/include /usr/local/opt/xz/include; do
    if [ -f "$inc/lzma.h" ] || [ -f "$inc/zlib.h" ]; then
        cflags="$cflags -I$inc"
    fi
done
for lib in /usr/local/lib /usr/pkg/lib /opt/local/lib /opt/homebrew/lib /usr/local/opt/xz/lib; do
    if [ -e "$lib/liblzma.so" ] || [ -e "$lib/liblzma.a" ] || [ -e "$lib/libz.so" ] || [ -e "$lib/libz.a" ]; then
        ldflags="-L$lib $ldflags"
    fi
done

# OpenBSD's base ld wants -Wl,-z,notext when a library is not position
# independent. It is ignored where the linker does not know the flag only
# if we do not pass it. Pass it only on OpenBSD.
if [ "$(uname -s)" = "OpenBSD" ]; then
    ldflags="$ldflags -Wl,-z,notext"
fi

echo "67zip: CXX=$CXX"
"$CXX" $cflags -c -o src/main.o src/main.cpp
"$CXX" $cflags -c -o src/port.o src/port.cpp
# DragonFly's base libstdc++ keeps std::filesystem in libstdc++fs. Other
# libstdc++ versions fold it into libstdc++ and do not ship that library,
# so only link it when the link fails without it.
if ! "$CXX" $cflags -o 67zip src/main.o src/port.o $ldflags; then
    "$CXX" $cflags -o 67zip src/main.o src/port.o $ldflags -lstdc++fs
fi
"$CXX" $cflags -o port_test src/port_test.cpp src/port.cpp
./port_test

stage=$(mktemp -d)
mkdir -p "$stage/usr/bin" "$stage/usr/share/man/man1"
# Solaris and illumos install(1) search the path instead of copying the
# file named on the command line. cp is the same on every target.
cp 67zip "$stage/usr/bin/67zip"
cp packaging/67zip.1 "$stage/usr/share/man/man1/67zip.1"
cp README.md "$stage/README.md"
chmod 755 "$stage/usr/bin/67zip"
chmod 644 "$stage/usr/share/man/man1/67zip.1"

mkdir -p dist
name="67zip-${version}-${os}-${arch}"

if [ "$kind" = "freebsd" ] && command -v pkg >/dev/null 2>&1; then
    sh packaging/freebsd-pkg.sh "$stage" "$version" "$arch"
    rm -rf "$stage"
    exit 0
fi

# Solaris tar treats -C as a file operand, not an option. GNU tar, BSD tar
# and illumos tar accept `tar czf - -C dir .`. Fall back to copying the
# tree next to the archive and archiving that, which every tar accepts.
if tar czf "dist/${name}.tar.gz" -C "$stage" . 2>/dev/null; then
    :
else
    pack=$(mktemp -d)
    cp -R "$stage/." "$pack/67zip"
    (cd "$pack" && tar czf "$root/dist/${name}.tar.gz" 67zip)
    rm -rf "$pack"
fi
rm -rf "$stage"
echo "67zip: packed dist/${name}.tar.gz"
