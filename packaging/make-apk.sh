#!/bin/sh
# Alpine apk. Uses the abuild tools that ship with Alpine's apk-tools.
# A signature key is generated in the container and thrown away; the apk is
# installable with `apk add --allow-untrusted`.
set -eu
stage=$1
version=$2
arch=$3
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)

pkgdir=$(mktemp -d)
mkdir -p "$pkgdir/pkg/usr/bin" "$pkgdir/pkg/usr/share/man/man1"
install -m 755 "$stage/usr/bin/67zip" "$pkgdir/pkg/usr/bin/67zip"
install -m 644 "$stage/usr/share/man/man1/67zip.1" "$pkgdir/pkg/usr/share/man/man1/67zip.1"
cat > "$pkgdir/pkg/.PKGINFO" << EOF
# Package Data
pkgname = 67zip
pkgver = ${version}-r0
arch = ${arch}
size = 0
origin = 67zip
pkgdesc = command-line archiver with a .67z container
url = https://github.com/${GITHUB_REPOSITORY:-67zip/67zip}
license = MIT
depend = so:libz.so.1
depend = so:liblzma.so.5
EOF
mkdir -p "$root/dist"
tar -C "$pkgdir/pkg" -czf "$root/dist/67zip-${version}-r0-${arch}.apk" .
echo "67zip: packed $root/dist/67zip-${version}-r0-${arch}.apk"
