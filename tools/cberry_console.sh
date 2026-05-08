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
            -- bash -lc "exec login -f $USER_NAME"
    else
        echo "==> not on a real tty (probably SSH); launching on a free VT"
        exec openvt -s -w -- env FRAMEBUFFER=/dev/fb1 \
            fbterm --font-size="$DEFAULT_FONT_SIZE" \
            -- bash -lc "exec login -f $USER_NAME"
    fi
}

do_install() {
    need_root
    ensure_pkg

    echo "==> writing $SVC (autologin as $USER_NAME)"
    cat > "$SVC" <<EOF
[Unit]
Description=Console on C-Berry LCD (/dev/fb1) via fbterm
After=multi-user.target
ConditionPathExists=/dev/fb1

[Service]
Type=simple
Environment=FRAMEBUFFER=/dev/fb1
Environment=TERM=linux
ExecStart=/usr/bin/fbterm --font-size=${DEFAULT_FONT_SIZE} -- /bin/login -f ${USER_NAME}
Restart=always
RestartSec=2
StandardInput=tty
StandardOutput=tty
TTYPath=/dev/tty7
TTYReset=yes
TTYVHangup=yes

[Install]
WantedBy=multi-user.target
EOF
    systemctl daemon-reload
    systemctl enable --now cberry-console.service
    echo "==> done. systemctl status cberry-console"
}

do_remove() {
    need_root
    systemctl disable --now cberry-console.service 2>/dev/null || true
    rm -f "$SVC"
    systemctl daemon-reload
    echo "==> removed"
}

case "${1:-status}" in
    run)     do_run ;;
    install) do_install ;;
    remove)  do_remove ;;
    status)  systemctl status cberry-console.service --no-pager 2>&1 || true ;;
    *) echo "usage: $0 {run|install|remove|status}" >&2; exit 1 ;;
esac
