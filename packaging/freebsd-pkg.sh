#!/bin/sh
# Build a FreeBSD pkg from a staged prefix. Called by stage.sh.
set -eu
stage=$1
version=$2
arch=$3
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$root"

meta=$(mktemp -d)
cat > "$meta/+MANIFEST" << EOF
name: 67zip
version: "${version}"
origin: archivers/67zip
comment: command-line archiver, .67z container
desc: command-line archiver with the p7zip command set
maintainer: 67zip
www: https://github.com/${GITHUB_REPOSITORY:-67zip/67zip}
arch: ${arch}
prefix: /usr/local
EOF
echo "/usr/bin/67zip" > "$meta/plist"
echo "/usr/share/man/man1/67zip.1" >> "$meta/plist"

mkdir -p dist
pkg create -M "$meta/+MANIFEST" -p "$meta/plist" -r "$stage" -o dist
rm -rf "$meta"
echo "67zip: packed FreeBSD pkg into dist/"
