#!/bin/sh
# iOS cross build. This does not run the test binary (wrong architecture).
#
#   SDK=iphoneos ./build/ios.sh
#   SDK=iphonesimulator ./build/ios.sh
#
# Needs Xcode. zlib is in the SDK. liblzma is not: set LZMA_PREFIX to a
# static liblzma built for the same SDK, or the link fails and says so.
set -eu
cd "$(dirname "$0")"
: "${SDK:=iphoneos}"
: "${ARCH:=arm64}"
if ! command -v xcrun >/dev/null 2>&1; then
    echo "67zip: xcrun not found. Install Xcode and its command-line tools." >&2
    exit 127
fi
SDKROOT=$(xcrun --sdk "$SDK" --show-sdk-path)
CXX=$(xcrun --sdk "$SDK" --find clang++)
MIN="-miphoneos-version-min=${IPHONEOS_DEPLOYMENT_TARGET:-12.0}"
case "$SDK" in
    *simulator*) MIN="-mios-simulator-version-min=${IPHONEOS_DEPLOYMENT_TARGET:-12.0}" ;;
esac
export CXX
export SKIP_TEST=1
export PKG_CONFIG=false
export EXTRA_CXXFLAGS="-arch ${ARCH} -isysroot ${SDKROOT} ${MIN} -fvisibility=hidden"
export EXTRA_CPPFLAGS="${LZMA_PREFIX:+-I${LZMA_PREFIX}/include}"
export EXTRA_LDFLAGS="-arch ${ARCH} -isysroot ${SDKROOT} ${LZMA_PREFIX:+-L${LZMA_PREFIX}/lib}"
if [ -z "${LZMA_PREFIX:-}" ]; then
    echo "67zip: LZMA_PREFIX is unset. liblzma is not in the iOS SDK." >&2
    echo "67zip: build xz for ${SDK} and re-run with LZMA_PREFIX=/path" >&2
    exit 1
fi
exec ./build-posix.sh "$@"
