#!/usr/bin/env bash
# tools/cberry_backlight.sh — get/set C-Berry backlight brightness.
#
# Usage:
#   ./cberry_backlight.sh           # print current value
#   sudo ./cberry_backlight.sh 128  # set value (0..255)
#   sudo ./cberry_backlight.sh off  # alias for 0
#   sudo ./cberry_backlight.sh max  # alias for 255

set -euo pipefail

BL=/sys/class/backlight/cberryfb
[[ -d $BL ]] || { echo "backlight $BL not present (driver loaded?)" >&2; exit 1; }

if [[ $# -eq 0 ]]; then
    echo "current=$(cat "$BL/brightness")  max=$(cat "$BL/max_brightness")"
    exit 0
fi

case $1 in
    off) val=0 ;;
    max) val=255 ;;
    *)   val=$1 ;;
esac

if [[ $EUID -ne 0 ]]; then
    echo "must run as root to change brightness" >&2
    exit 1
fi

echo "$val" > "$BL/brightness"
echo "brightness set to $val"
