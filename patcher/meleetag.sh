#!/bin/sh
# Linux/macOS counterpart of MeleeTag.bat:
#   ./meleetag.sh "Super Smash Bros. Melee (v1.02).iso"
# Extra options go in MELEETAG_OPTIONS, e.g. MELEETAG_OPTIONS="--cooldown 45".
here=$(cd "$(dirname "$0")" && pwd)
if [ $# -lt 1 ]; then
    echo "usage: $0 <melee.iso>" >&2
    exit 1
fi
# shellcheck disable=SC2086
exec "$here/meleetag-patch" $MELEETAG_OPTIONS "$1"
