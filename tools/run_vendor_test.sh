#!/usr/bin/env bash
# tools/run_vendor_test.sh — build & run the original admatec vendor demo.
#
# This bypasses our kernel driver entirely: the vendor code talks to
# /dev/mem via libbcm2835 (direct BCM2835 register poking). It is the
# "definitive" reference implementation.
#
# Usage:
#   sudo ./tools/run_vendor_test.sh        # builds + runs the BMP demo
#   sudo ./tools/run_vendor_test.sh build  # builds only
#   sudo ./tools/run_vendor_test.sh clean  # removes build artefacts
#
# After running, reload our driver with:
#   sudo modprobe cberryfb

set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
VENDOR_DIR="$REPO_DIR/C-Berry/SW/tft_test"

need_root() {
    if [[ $EUID -ne 0 ]]; then
        echo "must run as root (uses /dev/mem)" >&2
        exit 1
    fi
}

ensure_libbcm2835() {
    if ldconfig -p | grep -q libbcm2835; then
        return
    fi
    echo "==> installing libbcm2835 from upstream tarball"
    local tmp
    tmp=$(mktemp -d)
    pushd "$tmp" >/dev/null
    curl -sSL http://www.airspayce.com/mikem/bcm2835/bcm2835-1.75.tar.gz \
        -o bcm2835.tar.gz
    tar xzf bcm2835.tar.gz
    cd bcm2835-*/
    ./configure --prefix=/usr/local
    make -j"$(nproc)"
    make install
    ldconfig
    popd >/dev/null
    rm -rf "$tmp"
}

free_hardware() {
    echo "==> unloading cberryfb driver"
    modprobe -r cberryfb 2>/dev/null || true

    if grep -q '^dtoverlay=cberry' /boot/firmware/config.txt 2>/dev/null; then
        echo "!! WARNING: dtoverlay=cberry is still active in /boot/firmware/config.txt"
        echo "   The vendor code drives the same GPIOs directly and will fight"
        echo "   the SPI core. Comment out the line and reboot, or run anyway"
        echo "   to see what happens."
        read -rp "   continue? [y/N] " ans
        [[ "$ans" =~ ^[Yy]$ ]] || exit 1
    fi
}

do_build() {
    cd "$VENDOR_DIR"
    echo "==> building vendor demo in $VENDOR_DIR"
    # Original makefile uses 4-space tabs and -lbcm2835; works as-is on Pi.
    make clean >/dev/null 2>&1 || true
    make
    echo "==> built $VENDOR_DIR/tft_test"
}

do_run() {
    free_hardware
    cd "$VENDOR_DIR"
    echo "==> running ./tft_test (vendor BMP demo)"
    echo "    panel should show admatec test bitmap if HW is OK."
    ./tft_test
}

case "${1:-run}" in
    build) do_build ;;
    clean) cd "$VENDOR_DIR" && make clean ;;
    run|"")
        need_root
        ensure_libbcm2835
        do_build
        do_run
        ;;
    *)
        echo "usage: $0 [build|run|clean]" >&2
        exit 1
        ;;
esac
