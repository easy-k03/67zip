#!/bin/sh
# Cross builds that need a target liblzma. Builds xz for the target, then
# 67zip, then a tarball or a zip.
#
#   KIND=windows HOST=x86_64-w64-mingw32 CXX=x86_64-w64-mingw32-g++ packaging/cross.sh
#   KIND=android HOST=aarch64-linux-android packaging/cross.sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$root"
: "${KIND:?set KIND}"
: "${HOST:?set HOST}"

workdir=$(mktemp -d)
prefix="$workdir/prefix"

# xz is small and is the only dependency that is not in a cross sysroot.
xzver=5.6.3
curl -fsSL "https://github.com/tukaani-project/xz/releases/download/v${xzver}/xz-${xzver}.tar.gz" -o "$workdir/xz.tar.gz"
tar -C "$workdir" -xzf "$workdir/xz.tar.gz"

if [ "$KIND" = "android" ]; then
    if [ -z "${ANDROID_NDK:-}" ]; then
        ndk=$(ls -d "$HOME/Android/Sdk/ndk/"* 2>/dev/null | head -1 || true)
        if [ -z "$ndk" ]; then
            echo "67zip: ANDROID_NDK is not set and no NDK was found." >&2
            echo "67zip: the android job needs the NDK installed before packaging/cross.sh." >&2
            exit 1
        fi
        ANDROID_NDK=$ndk
    fi
    toolchain="$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64"
    export CC="$toolchain/bin/${HOST}24-clang"
    export CXX="$toolchain/bin/${HOST}24-clang++"
    (cd "$workdir/xz-${xzver}" && ./configure --host="$HOST" --prefix="$prefix" CC="$CC" && make -j"$(nproc)" && make install)
    "$CXX" -std=c++17 -O2 -Wall -Wextra -I"$prefix/include" -L"$prefix/lib" \
        -o "$workdir/67zip" src/main.cpp src/port.cpp -lz -llzma
    mkdir -p dist
    tar -C "$workdir" -czf "dist/67zip-android-${HOST}.tar.gz" 67zip
    exit 0
fi

export CC="${HOST}-gcc"
export CXX="${CXX:-${HOST}-g++}"
# Debian's mingw sysroot has neither zlib.h nor liblzma. Build both for
# the target. --disable-shared skips the libtool wrapper that cannot map a
# Linux build directory onto a mingw host path.
zlibver=1.3.1
curl -fsSL "https://zlib.net/zlib-${zlibver}.tar.gz" -o "$workdir/zlib.tar.gz" \
    || curl -fsSL "https://github.com/madler/zlib/releases/download/v${zlibver}/zlib-${zlibver}.tar.gz" -o "$workdir/zlib.tar.gz"
tar -C "$workdir" -xzf "$workdir/zlib.tar.gz"
(cd "$workdir/zlib-${zlibver}" && CHOST="$HOST" CC="$CC" ./configure --prefix="$prefix" --static && make -j"$(nproc)" && make install)
(cd "$workdir/xz-${xzver}" && ./configure --host="$HOST" --prefix="$prefix" --disable-shared --disable-nls && make -j"$(nproc)" && make install)
"$CXX" -std=c++17 -O2 -Wall -Wextra -I"$prefix/include" -static \
    -o "$workdir/67zip.exe" src/main.cpp src/port.cpp "$prefix/lib/liblzma.a" "$prefix/lib/libz.a"
if command -v x86_64-w64-mingw32-objdump >/dev/null 2>&1; then
    deps=$(x86_64-w64-mingw32-objdump -p "$workdir/67zip.exe" | awk '/DLL Name/{print $3}')
    echo "$deps"
    echo "$deps" | grep -E 'liblzma|libz|libgcc|libstdc|libwinpthread' && {
        echo "67zip: 67zip.exe still imports a non-system DLL." >&2
        exit 1
    }
fi
mkdir -p dist
version=$(git describe --tags --always 2>/dev/null || echo 0.0.0)
version=${version#v}
(cd "$workdir" && zip -j "$root/dist/67zip-${version}-${KIND}-${HOST}.zip" 67zip.exe)
echo "67zip: packed dist/67zip-${version}-${KIND}-${HOST}.zip"
