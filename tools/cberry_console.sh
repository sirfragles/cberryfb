#!/usr/bin/env bash
# tools/cberry_console.sh — run a real terminal directly on the
# C-Berry LCD (/dev/fb1) using fbterm.
#
# Unlike cberry_mirror.sh (which depended on the legacy VC4 userland
# and is broken on 64-bit Bookworm), this needs no GPU bits at all —
# fbterm draws the terminal cells straight into the framebuffer.
#
# Two modes:
#
#   sudo ./cberry_console.sh run       # one-shot, foreground
#   sudo ./cberry_console.sh install   # systemd service on tty + autologin
#   sudo ./cberry_console.sh remove
#   sudo ./cberry_console.sh status
#
# 'install' wires fbterm into a getty so you get a usable login on
# C-Berry across reboots, leaving HDMI free for tty1.

set -euo pipefail

USER_NAME="${SUDO_USER:-${USER}}"
SVC=/etc/systemd/system/cberry-console.service
DEFAULT_FONT_SIZE=8

need_root() {
    if [[ $EUID -ne 0 ]]; then
        echo "must run as root" >&2
        exit 1
    fi
}

ensure_pkg() {
    if ! command -v fbterm >/dev/null 2>&1; then
        echo "==> installing fbterm"
        apt-get update -qq
        apt-get install -y -qq fbterm
    fi
}

do_run() {
    need_root
    ensure_pkg
    # fbterm refuses to start unless stdin is a real TTY (it ioctls
    # KDSETMODE etc.). When run from SSH/pty that fails with
    # "stdin isn't a interactive tty". Spawn it on a free VT instead;
    # openvt allocates one, runs us there, and cleans up on exit.
    if [[ -t 0 && -c /dev/tty ]]; then
        FRAMEBUFFER=/dev/fb1 exec fbterm \
            --font-size="$DEFAULT_FONT_SIZE" \
            -- su - "$USER_NAME"
    else
        echo "==> not on a real tty (probably SSH); launching on a free VT"
        exec openvt -s -w -- env FRAMEBUFFER=/dev/fb1 \
            fbterm --font-size="$DEFAULT_FONT_SIZE" \
            -- su - "$USER_NAME"
    fi
}

do_install() {
    need_root
    ensure_pkg

    echo "==> writing $SVC (autologin as $USER_NAME, on tty2)"
    cat > "$SVC" <<EOF
[Unit]
Description=Console on C-Berry LCD (/dev/fb1) via fbterm
After=systemd-user-sessions.service plymouth-quit-wait.service
ConditionPathExists=/dev/fb1
Conflicts=getty@tty2.service

[Service]
# fbterm 1.7 hard-codes /dev/fb0 (the FRAMEBUFFER env var is ignored
# in this build), so bind-mount /dev/fb1 over /dev/fb0 *inside this
# unit's mount namespace*. The host's /dev/fb0 is not affected.
PrivateMounts=yes
BindPaths=/dev/fb1:/dev/fb0
Environment=TERM=linux
ExecStart=/usr/bin/fbterm --font-size=${DEFAULT_FONT_SIZE} -- /bin/su - ${USER_NAME}
Type=idle
Restart=always
RestartSec=2
UtmpIdentifier=tty2
TTYPath=/dev/tty2
TTYReset=yes
TTYVHangup=yes
TTYVTDisallocate=yes
StandardInput=tty
StandardOutput=tty
StandardError=journal
IgnoreSIGPIPE=no
SendSIGHUP=yes

[Install]
WantedBy=multi-user.target
EOF
    systemctl daemon-reload
    # Make sure the regular getty doesn't fight us for tty2.
    systemctl disable --now getty@tty2.service 2>/dev/null || true
    systemctl enable --now cberry-console.service
    echo "==> done. systemctl status cberry-console"
}

do_remove() {
    need_root
    systemctl disable --now cberry-console.service 2>/dev/null || true
    rm -f "$SVC"
    systemctl daemon-reload
    # Bring the regular tty2 getty back if it was disabled.
    systemctl enable --now getty@tty2.service 2>/dev/null || true
    echo "==> removed"
}

case "${1:-status}" in
    run)     do_run ;;
    install) do_install ;;
    remove)  do_remove ;;
    start)   need_root; systemctl start cberry-console.service ;;
    stop)    need_root; systemctl stop  cberry-console.service ;;
    restart) need_root; systemctl restart cberry-console.service ;;
    status)  systemctl status cberry-console.service --no-pager 2>&1 || true ;;
    *) echo "usage: $0 {run|install|remove|start|stop|restart|status}" >&2; exit 1 ;;
esac
