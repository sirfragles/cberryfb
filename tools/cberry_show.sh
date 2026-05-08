#!/usr/bin/env bash
# tools/cberry_show.sh — display an image / play a video on /dev/fb1.
# Wraps fbi (images) and ffmpeg (video) so you don't have to remember
# the framebuffer flags.
#
# Usage:
#   sudo ./cberry_show.sh image <path>       # picture; Ctrl+C to exit
#   sudo ./cberry_show.sh slides <dir> [sec] # cycle images, default 5 s
#   sudo ./cberry_show.sh video <path>       # play video scaled to 320x240
#   sudo ./cberry_show.sh clear              # blank the screen

set -euo pipefail

FB=/dev/fb1

need_root() {
    if [[ $EUID -ne 0 ]]; then
        echo "must run as root (writes to $FB)" >&2
        exit 1
    fi
}

ensure() {
    local pkg=$1 cmd=$2
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "==> installing $pkg"
        apt-get update -qq
        apt-get install -y -qq "$pkg"
    fi
}

case "${1:-}" in
    image)
        need_root
        ensure fbi fbi
        shift
        fbi -d "$FB" -T 1 -a --noverbose "$@"
        ;;
    slides)
        need_root
        ensure fbi fbi
        dir=${2:?slides <dir> [sec]}
        sec=${3:-5}
        fbi -d "$FB" -T 1 -a --noverbose -t "$sec" "$dir"/*
        ;;
    video)
        need_root
        ensure ffmpeg ffmpeg
        path=${2:?video <path>}
        ffmpeg -re -i "$path" -pix_fmt rgb565le -s 320x240 -f fbdev "$FB"
        ;;
    clear)
        need_root
        # zero out the whole framebuffer
        dd if=/dev/zero of="$FB" bs=153600 count=1 status=none || true
        ;;
    *)
        echo "usage: $0 {image|slides|video|clear} ..." >&2
        exit 1
        ;;
esac
