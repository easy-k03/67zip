#!/bin/sh
# Run inside a distro container with the source mounted at /src.
# Usage: in-container.sh deb|rpm|apk|arch
set -eu
kind=$1
cd /src

case "$kind" in
deb)
    export DEBIAN_FRONTEND=noninteractive
    apt-get update
    apt-get install -y build-essential zlib1g-dev liblzma-dev pkg-config fakeroot
    ;;
rpm)
    dnf install -y gcc-c++ make zlib-devel xz-devel pkgconf rpm-build
    ;;
apk)
    apk add build-base zlib-dev xz-dev pkgconf
    ;;
arch)
    pacman -Sy --noconfirm base-devel zlib xz pkgconf
    ;;
*)
    echo "unknown kind $kind" >&2
    exit 2
    ;;
esac

make -j"$(nproc)"
make test

version=$(git describe --tags --always 2>/dev/null || echo 0.0.0)
version=${version#v}
# rpm Version is an EVR token. A checkout with no tag describes as a hash,
# which rpm rejects. Package builds that are not a release use 0.0.0.
case "$version" in
    *[!A-Za-z0-9._+]*) version=0.0.0 ;;
esac
arch=$(uname -m)
mkdir -p dist

stage=$(mktemp -d)
mkdir -p "$stage/usr/bin" "$stage/usr/share/man/man1"
install -m 755 67zip "$stage/usr/bin/67zip"
install -m 644 packaging/67zip.1 "$stage/usr/share/man/man1/67zip.1"

case "$kind" in
deb)  sh packaging/make-deb.sh "$stage" "$version" "$arch" ;;
rpm)  sh packaging/make-rpm.sh "$stage" "$version" "$arch" ;;
apk)  sh packaging/make-apk.sh "$stage" "$version" "$arch" ;;
arch) sh packaging/make-arch.sh "$stage" "$version" "$arch" ;;
esac
rm -rf "$stage"
