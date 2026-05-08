#!/usr/bin/env bash
# Cross-compile cberryfb.ko on macOS using a Linux container.
#
# Usage:
#
#   # Auto-detect everything from a running Pi via SSH:
#   PI_HOST=pi@raspberrypi.local ./build-cross.sh
#
#   # Manual:
#   ./build-cross.sh                                   # default: Pi 2/3/Zero2W 32-bit, rpi-6.6.y
#   ARCH=arm64 DEFCONFIG=bcm2711_defconfig \
#       CROSS_COMPILE=aarch64-linux-gnu- ./build-cross.sh
#   KERNEL_BRANCH=rpi-6.12.y ./build-cross.sh
#
# Outputs: ./out/cberryfb.ko and ./out/cberry.dtbo
#
# Auto-detect mode (when PI_HOST is set):
#   - Reads `uname -r`, `uname -m`, /proc/device-tree/model via SSH and picks
#     KERNEL_BRANCH, ARCH, CROSS_COMPILE and DEFCONFIG accordingly.
#   - Override any of the variables on the command line to force a value.
#   - After the build, verifies that the .ko vermagic matches the Pi's uname -r.

set -euo pipefail
cd "$(dirname "$0")"

# ---------------------------------------------------------------------------
# Auto-detect from a running Pi (optional)
# ---------------------------------------------------------------------------
UNAME_R=""
if [[ -n "${PI_HOST:-}" ]]; then
    echo "==> Probing ${PI_HOST} for kernel/arch info..."
    PI_INFO="$(ssh -o BatchMode=yes -o ConnectTimeout=5 "${PI_HOST}" '
        printf "PI_UNAME_R=%s\n" "$(uname -r)"
        printf "PI_UNAME_M=%s\n" "$(uname -m)"
        printf "PI_MODEL=%s\n"   "$(tr -d \\\\0 < /proc/device-tree/model 2>/dev/null || echo unknown)"
    ')" || { echo "ERROR: SSH to ${PI_HOST} failed" >&2; exit 1; }
    eval "${PI_INFO}"
    UNAME_R="${PI_UNAME_R}"
    echo "    uname -r: ${PI_UNAME_R}"
    echo "    uname -m: ${PI_UNAME_M}"
    echo "    model:    ${PI_MODEL}"

    # Map kernel version (e.g. 6.6.78-v7+) to raspberrypi/linux branch.
    PI_KVER="$(echo "${PI_UNAME_R}" | sed -E 's/^([0-9]+\.[0-9]+).*/\1/')"
    : "${KERNEL_BRANCH:=rpi-${PI_KVER}.y}"

    # Map architecture/suffix to ARCH + CROSS_COMPILE + DEFCONFIG.
    case "${PI_UNAME_M}" in
        aarch64)
            : "${ARCH:=arm64}"
            : "${CROSS_COMPILE:=aarch64-linux-gnu-}"
            : "${DEFCONFIG:=bcm2711_defconfig}"
            ;;
        armv7l|armv7*)
            : "${ARCH:=arm}"
            : "${CROSS_COMPILE:=arm-linux-gnueabihf-}"
            : "${DEFCONFIG:=bcm2709_defconfig}"
            ;;
        armv6l|armv6*)
            : "${ARCH:=arm}"
            : "${CROSS_COMPILE:=arm-linux-gnueabihf-}"
            : "${DEFCONFIG:=bcmrpi_defconfig}"
            ;;
        *)
            echo "ERROR: unsupported uname -m '${PI_UNAME_M}'" >&2
            exit 1
            ;;
    esac
fi

# ---------------------------------------------------------------------------
# Defaults (Pi 2/3/Zero 2 W, 32-bit, kernel 6.6.y) when nothing was set.
# ---------------------------------------------------------------------------
KERNEL_BRANCH="${KERNEL_BRANCH:-rpi-6.6.y}"
DEFCONFIG="${DEFCONFIG:-bcm2709_defconfig}"
ARCH="${ARCH:-arm}"
CROSS_COMPILE="${CROSS_COMPILE:-arm-linux-gnueabihf-}"

echo "==> Build configuration"
printf "    KERNEL_BRANCH = %s\n" "${KERNEL_BRANCH}"
printf "    DEFCONFIG     = %s\n" "${DEFCONFIG}"
printf "    ARCH          = %s\n" "${ARCH}"
printf "    CROSS_COMPILE = %s\n" "${CROSS_COMPILE}"

if ! docker info >/dev/null 2>&1; then
    echo "ERROR: no Docker engine reachable. Start Colima first:" >&2
    echo "    colima start --vm-type=vz --cpu 4 --memory 6 --disk 30 --arch aarch64" >&2
    exit 1
fi

mkdir -p out
docker buildx build \
    --file Dockerfile.cross \
    --target export \
    --build-arg "KERNEL_BRANCH=${KERNEL_BRANCH}" \
    --build-arg "DEFCONFIG=${DEFCONFIG}" \
    --build-arg "ARCH=${ARCH}" \
    --build-arg "CROSS_COMPILE=${CROSS_COMPILE}" \
    --output type=local,dest=./out \
    --progress plain \
    .

echo
echo "==> Build artifacts:"
ls -la ./out/

# ---------------------------------------------------------------------------
# Verify vermagic against the live Pi (when probing is available).
# ---------------------------------------------------------------------------
if [[ -n "${UNAME_R}" && -f ./out/cberryfb.ko ]]; then
    echo
    echo "==> Verifying vermagic..."
    KO_VERMAGIC="$(docker run --rm -v "$(pwd)/out:/out" debian:bookworm-slim \
        bash -c 'apt-get update -qq && apt-get install -y -qq kmod >/dev/null \
                 && modinfo -F vermagic /out/cberryfb.ko' 2>/dev/null \
        | head -n1)"
    KO_VER="$(echo "${KO_VERMAGIC}" | awk '{print $1}')"
    echo "    .ko vermagic: ${KO_VERMAGIC}"
    echo "    Pi  uname -r: ${UNAME_R}"
    if [[ "${KO_VER}" == "${UNAME_R}" ]]; then
        echo "    OK: versions match."
    else
        echo "    WARNING: kernel versions differ. The module will refuse to load."
        echo "    Either update the Pi (sudo apt full-upgrade) or rebuild with"
        echo "    KERNEL_BRANCH matching the Pi's kernel."
    fi
fi
