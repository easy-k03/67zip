#!/bin/sh
# Arch Linux pkg.tar.zst. makepkg needs a PKGBUILD and a non-root user on
# some releases; building the archive directly keeps the container simple.
set -eu
stage=$1
version=$2
arch=$3
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)

pkgdir=$(mktemp -d)
mkdir -p "$pkgdir/usr/bin" "$pkgdir/usr/share/man/man1"
install -m 755 "$stage/usr/bin/67zip" "$pkgdir/usr/bin/67zip"
install -m 644 "$stage/usr/share/man/man1/67zip.1" "$pkgdir/usr/share/man/man1/67zip.1"
size=$(wc -c < "$pkgdir/usr/bin/67zip")
cat > "$pkgdir/.PKGINFO" << EOF
pkgname = 67zip
pkgver = ${version}-1
pkgdesc = command-line archiver with a .67z container
url = https://github.com/${GITHUB_REPOSITORY:-67zip/67zip}
builddate = $(date +%s)
size = ${size}
arch = ${arch}
license = MIT
depend = zlib
depend = xz
EOF
mkdir -p "$root/dist"
name="67zip-${version}-1-${arch}.pkg.tar.zst"
tar -C "$pkgdir" -cf - . | zstd -T0 -19 -o "$root/dist/$name"
echo "67zip: packed $root/dist/$name"
