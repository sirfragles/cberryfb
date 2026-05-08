#!/usr/bin/env bash
# tools/cberry_mirror.sh — mirror HDMI (/dev/fb0) onto C-Berry LCD (/dev/fb1).
#
# Uses raspi2fb (lightweight, works on Pi 3A+, no GPU dependency).
# Installs raspi2fb from source if missing, then enables a systemd
# service to keep the mirror running across reboots.
#
# Usage:
#   sudo ./cberry_mirror.sh install    # build, install, enable service
#   sudo ./cberry_mirror.sh start      # start the running mirror
#   sudo ./cberry_mirror.sh stop       # stop it
#   sudo ./cberry_mirror.sh remove     # disable + uninstall
#   sudo ./cberry_mirror.sh status

set -euo pipefail

BIN=/usr/local/bin/raspi2fb
SVC=/etc/systemd/system/cberry-mirror.service
SRC=https://github.com/AndrewFromMelbourne/raspi2fb.git

need_root() {
    if [[ $EUID -ne 0 ]]; then
        echo "must run as root" >&2
        exit 1
    fi
}

do_install() {
    need_root
    if [[ ! -x $BIN ]]; then
        echo "==> installing build deps"
        apt-get update -qq
        apt-get install -y -qq git cmake build-essential libbsd-dev \
            libraspberrypi-dev libraspberrypi0
        local tmp
        tmp=$(mktemp -d)
        git clone --depth=1 "$SRC" "$tmp/raspi2fb"
        cd "$tmp/raspi2fb"
        mkdir -p build && cd build
        cmake .. && make -j"$(nproc)"
        install -m 0755 raspi2fb "$BIN"
        rm -rf "$tmp"
    fi

    echo "==> installing systemd unit"
    cat > "$SVC" <<'EOF'
[Unit]
Description=Mirror HDMI framebuffer to C-Berry LCD
After=multi-user.target

[Service]
Type=simple
ExecStart=/usr/local/bin/raspi2fb --device /dev/fb1 --fps 20
Restart=always

[Install]
WantedBy=multi-user.target
EOF
    systemctl daemon-reload
    systemctl enable --now cberry-mirror.service
    echo "==> mirror running. systemctl status cberry-mirror"
}

do_remove() {
    need_root
    systemctl disable --now cberry-mirror.service 2>/dev/null || true
    rm -f "$SVC" "$BIN"
    systemctl daemon-reload
    echo "==> removed"
}

case "${1:-status}" in
    install)  do_install ;;
    remove)   do_remove ;;
    start)    need_root; systemctl start cberry-mirror.service ;;
    stop)     need_root; systemctl stop  cberry-mirror.service ;;
    status)   systemctl status cberry-mirror.service --no-pager 2>&1 || true ;;
    *)        echo "usage: $0 {install|remove|start|stop|status}" >&2; exit 1 ;;
esac
