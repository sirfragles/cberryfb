#!/usr/bin/env bash
# tools/cberry_autobacklight.sh — install systemd unit that turns the
# backlight on after the system is fully booted, avoiding the flash of
# black during driver load.
#
# The kernel module now keeps brightness at 0 on probe so /dev/fb1
# initialises silently. This unit waits for multi-user.target and then
# ramps brightness to the user-chosen value (default 255).
#
# Usage:
#   sudo ./cberry_autobacklight.sh install [value]   # value 0..255
#   sudo ./cberry_autobacklight.sh remove
#   sudo ./cberry_autobacklight.sh status

set -euo pipefail

SVC=/etc/systemd/system/cberry-backlight.service

need_root() {
    if [[ $EUID -ne 0 ]]; then
        echo "must run as root" >&2
        exit 1
    fi
}

case "${1:-status}" in
    install)
        need_root
        val=${2:-255}
        if ! [[ $val =~ ^[0-9]+$ ]] || (( val < 0 || val > 255 )); then
            echo "value must be 0..255" >&2; exit 1
        fi
        cat > "$SVC" <<EOF
[Unit]
Description=Bring up C-Berry LCD backlight after boot
After=multi-user.target
ConditionPathExists=/sys/class/backlight/cberryfb

[Service]
Type=oneshot
ExecStart=/bin/sh -c 'echo $val > /sys/class/backlight/cberryfb/brightness'
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF
        systemctl daemon-reload
        systemctl enable --now cberry-backlight.service
        echo "==> backlight will rise to $val after each boot"
        ;;
    remove)
        need_root
        systemctl disable --now cberry-backlight.service 2>/dev/null || true
        rm -f "$SVC"
        systemctl daemon-reload
        echo "==> removed; module-default brightness is 0"
        ;;
    status)
        systemctl status cberry-backlight.service --no-pager 2>&1 || true
        ;;
    *)
        echo "usage: $0 {install [value]|remove|status}" >&2
        exit 1
        ;;
esac
