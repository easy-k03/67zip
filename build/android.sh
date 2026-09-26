#!/bin/sh
# Android NDK cross build. Does not run tests (wrong ABI).
#
#   ANDROID_NDK=/path/to/ndk ./build/android.sh
#   ABI=arm64-v8a API=24 ANDROID_NDK=... ./build/android.sh
#
# The NDK has zlib. It does not have liblzma. Set LZMA_PREFIX to a static
# liblzma built with the same toolchain.
set -eu
cd "$(dirname "$0")"
: "${ABI:=arm64-v8a}"
: "${API:=24}"
if [ -z "${ANDROID_NDK:-}" ]; then
    echo "67zip: set ANDROID_NDK to the NDK root." >&2
    exit 1
fi
HOST_TAG=linux-x86_64
case "$(uname -s)" in
    Darwin) HOST_TAG=darwin-x86_64 ;;
    MINGW*|MSYS*) HOST_TAG=windows-x86_64 ;;
esac
TOOLCHAIN="$ANDROID_NDK/toolchains/llvm/prebuilt/$HOST_TAG"
if [ ! -d "$TOOLCHAIN" ]; then
    echo "67zip: NDK toolchain not found: $TOOLCHAIN" >&2
    exit 1
fi
case "$ABI" in
    arm64-v8a)   TRIPLE=aarch64-linux-android ;;
    armeabi-v7a) TRIPLE=armv7a-linux-androideabi ;;
    x86_64)      TRIPLE=x86_64-linux-android ;;
    x86)         TRIPLE=i686-linux-android ;;
    *) echo "67zip: unknown ABI $ABI" >&2; exit 1 ;;
esac
export CXX="$TOOLCHAIN/bin/${TRIPLE}${API}-clang++"
export SKIP_TEST=1
export PKG_CONFIG=false
export EXTRA_CXXFLAGS="--target=${TRIPLE}${API}"
export EXTRA_CPPFLAGS="${LZMA_PREFIX:+-I${LZMA_PREFIX}/include}"
export EXTRA_LDFLAGS="${LZMA_PREFIX:+-L${LZMA_PREFIX}/lib}"
if [ -z "${LZMA_PREFIX:-}" ]; then
    echo "67zip: LZMA_PREFIX is unset. The NDK does not ship liblzma." >&2
    echo "67zip: cross-build xz for ${TRIPLE} and re-run with LZMA_PREFIX=/path" >&2
    exit 1
fi
exec ./build-posix.sh "$@"
