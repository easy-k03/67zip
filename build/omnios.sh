#!/bin/sh
# OmniOS is illumos. Kept as its own entry so a package script can call it.
set -eu
cd "$(dirname "$0")"
exec ./illumos.sh "$@"
