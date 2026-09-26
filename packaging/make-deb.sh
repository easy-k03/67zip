#!/bin/sh
# Debian / Ubuntu package. No dpkg-deb dependency beyond dpkg, which the
# container already has.
set -eu
stage=$1
version=$2
arch=$3
case "$arch" in
    x86_64) debarch=amd64 ;;
    aarch64) debarch=arm64 ;;
    *) debarch=$arch ;;
esac
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
debdir=$(mktemp -d)
mkdir -p "$debdir/DEBIAN" "$debdir/usr"
cp -a "$stage/usr/bin" "$debdir/usr/bin"
mkdir -p "$debdir/usr/share/man/man1"
cp -a "$stage/usr/share/man/man1/67zip.1" "$debdir/usr/share/man/man1/"
cat > "$debdir/DEBIAN/control" << EOF
Package: 67zip
Version: ${version}
Section: utils
Priority: optional
Architecture: ${debarch}
Depends: zlib1g, liblzma5
Maintainer: 67zip <67zip@localhost>
Description: command-line archiver with a .67z container
 67zip uses the same commands as p7zip. Archives end in .67z.
EOF
mkdir -p "$root/dist"
dpkg-deb --root-owner-group --build "$debdir" "$root/dist/67zip_${version}_${debarch}.deb"
echo "67zip: packed $root/dist/67zip_${version}_${debarch}.deb"
