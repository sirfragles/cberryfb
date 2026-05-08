#!/usr/bin/env bash
# tools/run_mintest.sh — build & run cberry_mintest.c (libbcm2835).
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

have_bcm2835() {
    [[ -f /usr/local/include/bcm2835.h || -f /usr/include/bcm2835.h ]] && \
    ls /usr/local/lib/libbcm2835.* /usr/lib/libbcm2835.* \
        /usr/lib/*/libbcm2835.* 2>/dev/null | grep -q .
}

if ! have_bcm2835; then
    echo "libbcm2835 missing — run tools/run_vendor_test.sh first" >&2
    exit 1
fi

echo "==> build"
gcc -O2 -Wall -o cberry_mintest cberry_mintest.c -lbcm2835 -lrt

if [[ $EUID -ne 0 ]]; then
    echo "must run as root" >&2
    exit 1
fi

modprobe -r cberryfb 2>/dev/null || true

COLOR="${1:-red}"
echo "==> running ./cberry_mintest $COLOR"
./cberry_mintest "$COLOR"
