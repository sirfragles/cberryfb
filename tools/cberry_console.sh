#!/usr/bin/env bash
# tools/cberry_console.sh — toggle Linux console mirroring onto /dev/fb1.
#
# Adds (or removes) `fbcon=map:10` to /boot/firmware/cmdline.txt so the
# kernel framebuffer console treats fb1 as the primary text console.
# After a reboot, login prompt + dmesg appear on the C-Berry LCD.
#
# Usage:
#   sudo ./cberry_console.sh on      # enable, then reboot
#   sudo ./cberry_console.sh off     # disable, then reboot
#   sudo ./cberry_console.sh status

set -euo pipefail

CMDLINE=/boot/firmware/cmdline.txt
[[ -f $CMDLINE ]] || CMDLINE=/boot/cmdline.txt

action=${1:-status}

case "$action" in
    on)
        if grep -q 'fbcon=map:' "$CMDLINE"; then
            sudo sed -i 's/fbcon=map:[0-9]*/fbcon=map:10/' "$CMDLINE"
        else
            sudo sed -i 's/$/ fbcon=map:10/' "$CMDLINE"
        fi
        echo "==> enabled fbcon mirror on fb1; reboot to apply"
        ;;
    off)
        sudo sed -i 's/ *fbcon=map:[0-9]*//g' "$CMDLINE"
        echo "==> disabled fbcon mirror; reboot to apply"
        ;;
    status)
        if grep -q 'fbcon=map:' "$CMDLINE"; then
            echo "fbcon mapping: $(grep -o 'fbcon=map:[0-9]*' "$CMDLINE")"
        else
            echo "fbcon mapping: not set (console only on fb0)"
        fi
        ;;
    *)
        echo "usage: $0 {on|off|status}" >&2
        exit 1
        ;;
esac
