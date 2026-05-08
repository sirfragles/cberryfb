#!/usr/bin/env bash
# tools/run_vendortest.sh — build cberry_vendortest.c linked against
# vendor's actual tft.c + RAIO8870.c sources. This is the strongest
# isolation test: if this fills the panel but cberry_mintest.c does
# not, the bug is in our mintest, not in vendor / hardware / system.
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/.." && pwd)"
VENDOR="$ROOT/C-Berry/SW/tft_test"
cd "$DIR"

if [[ ! -d "$VENDOR" ]]; then
    echo "vendor sources missing at $VENDOR" >&2
    exit 1
fi

if ! ldconfig -p | grep -q libbcm2835; then
    echo "libbcm2835 missing — run tools/run_vendor_test.sh once first" >&2
    exit 1
fi

echo "==> build (linking vendor tft.c + RAIO8870.c)"
gcc -O2 -Wall -I"$VENDOR" \
    -o cberry_vendortest \
    cberry_vendortest.c \
    "$VENDOR/tft.c" "$VENDOR/RAIO8870.c" \
    -lbcm2835 -lrt

if [[ $EUID -ne 0 ]]; then
    echo "must run as root" >&2
    exit 1
fi

modprobe -r cberryfb 2>/dev/null || true

COLOR="${1:-red}"
echo "==> running ./cberry_vendortest $COLOR"
./cberry_vendortest "$COLOR"
