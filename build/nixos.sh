#!/bin/sh
# NixOS / nixpkgs. There is no /usr and no /usr/local.
# Headers and libraries come from pkg-config inside a nix shell.
#
#   nix-shell -p zlib xz pkg-config --run ./build/nixos.sh
#   nix-shell -p zlib xz pkg-config --run 'PREFIX=$out ./build/nixos.sh'
#
# Do not pass -I/usr/include or -L/usr/lib.
set -eu
cd "$(dirname "$0")"
if ! command -v pkg-config >/dev/null 2>&1; then
    echo "67zip: pkg-config is required on NixOS." >&2
    echo "67zip: nix-shell -p zlib xz pkg-config --run $0" >&2
    exit 127
fi
if [ -z "${PREFIX:-}" ]; then
    echo "67zip: PREFIX is unset; building only, not installing." >&2
    echo "67zip: a Nix derivation must set PREFIX=\$out" >&2
fi
exec ./build-posix.sh "$@"
