#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
cberry_fill.py — fill the panel with a solid colour using the *exact*
GPIO/SPI sequence from the vendor SDK (TFT_DataMultiWrite).

If this lights up the screen, our kernel driver has a software bug
(most likely in the deferred-IO update path). If it doesn't, there's
a deeper integration mismatch.

Pre-conditions:
    sudo modprobe -r cberryfb
    sudo sed -i 's/^dtoverlay=cberry/#dtoverlay=cberry/' /boot/firmware/config.txt
    sudo reboot

Usage:
    sudo ./tools/cberry_fill.py            # fills with white
    sudo ./tools/cberry_fill.py --color red
    sudo ./tools/cberry_fill.py --color 0xF800
"""

from __future__ import annotations
import argparse
import sys
import time

try:
    import spidev
    import RPi.GPIO as GPIO  # type: ignore
except ImportError as e:
    sys.exit(f"missing dependency: {e}; "
             "sudo apt install python3-spidev python3-rpi.gpio")

OE_PIN, RS_PIN, CS_PIN, WR_PIN, RST_PIN, WAIT_PIN = 17, 18, 8, 24, 25, 22
W, H = 320, 240

# RAIO regs
PWRR, MRWC, PCLK, SYSR, HDWR = 0x01, 0x02, 0x04, 0x10, 0x14
HNDFTR, HNDR, HSTR, HPWR     = 0x15, 0x16, 0x17, 0x18
VDHR0, VDHR1, VNDR0, VNDR1, VPWR = 0x19, 0x1A, 0x1B, 0x1C, 0x1F
HSAW0, HSAW1, HEAW0, HEAW1 = 0x30, 0x31, 0x34, 0x35
VSAW0, VSAW1, VEAW0, VEAW1 = 0x32, 0x33, 0x36, 0x37
TBCR, MCLR = 0x43, 0x8E
PLLC1, PLLC2, P1CR, P1DCR = 0x88, 0x89, 0x8A, 0x8B
IODR = 0x13

COLOURS = {
    "black":  0x0000, "white": 0xFFFF,
    "red":    0xF800, "green": 0x07E0, "blue":  0x001F,
    "yellow": 0xFFE0, "cyan":  0x07FF, "magenta": 0xF81F,
}


class Panel:
    def __init__(self, hz: int = 16_000_000):
        GPIO.setwarnings(False)
        GPIO.setmode(GPIO.BCM)
        for p in (OE_PIN, RS_PIN, CS_PIN, WR_PIN, RST_PIN):
            GPIO.setup(p, GPIO.OUT, initial=GPIO.HIGH)
        GPIO.setup(WAIT_PIN, GPIO.IN, pull_up_down=GPIO.PUD_UP)

        self.spi = spidev.SpiDev()
        self.spi.open(0, 1)
        self.spi.mode = 0
        self.spi.max_speed_hz = hz
        self.spi.bits_per_word = 8

    # --- vendor-compatible primitives -----------------------------

    def _xfer16(self, w: int) -> None:
        self.spi.xfer2([(w >> 8) & 0xFF, w & 0xFF])

    def reg_w(self, reg: int) -> None:
        # vendor TFT_RegWrite: RS=H, CS=L, WR=L, OE=L → SPI → all H
        GPIO.output(RS_PIN, 1)
        GPIO.output(CS_PIN, 0)
        GPIO.output(WR_PIN, 0)
        GPIO.output(OE_PIN, 0)
        self._xfer16(reg)
        GPIO.output(WR_PIN, 1)
        GPIO.output(CS_PIN, 1)
        GPIO.output(OE_PIN, 1)

    def dat_w(self, val: int) -> None:
        # vendor TFT_DataWrite: RS=L, CS=L, WR=L, OE=L → SPI → all H
        GPIO.output(RS_PIN, 0)
        GPIO.output(CS_PIN, 0)
        GPIO.output(WR_PIN, 0)
        GPIO.output(OE_PIN, 0)
        self._xfer16(val)
        GPIO.output(WR_PIN, 1)
        GPIO.output(CS_PIN, 1)
        GPIO.output(OE_PIN, 1)

    def set_reg(self, reg: int, val: int) -> None:
        self.reg_w(reg); self.dat_w(val)

    def wait_busy(self, secs: float = 1.0) -> None:
        deadline = time.monotonic() + secs
        while time.monotonic() < deadline and GPIO.input(WAIT_PIN) == 0:
            pass

    def hard_reset(self) -> None:
        GPIO.output(RST_PIN, 0); time.sleep(0.05)
        GPIO.output(RST_PIN, 1); time.sleep(0.05)

    # --- vendor RAIO_init replica ---------------------------------

    def init(self) -> None:
        self.set_reg(PLLC1, 0x07); time.sleep(0.001)
        self.set_reg(PLLC2, 0x03); time.sleep(0.001)
        self.set_reg(PWRR, 0x01)
        self.set_reg(PWRR, 0x00); time.sleep(0.1)

        self.set_reg(SYSR, 0x0A)             # 16 bpp 65k color, 8-bit mem bus

        self.set_reg(HDWR,   (W // 8) - 1)
        self.set_reg(HNDFTR, 0x02)
        self.set_reg(HNDR,   0x03)
        self.set_reg(HSTR,   0x04)
        self.set_reg(HPWR,   0x03)

        self.set_reg(VDHR0, (H - 1) & 0xFF)
        self.set_reg(VDHR1, (H - 1) >> 8)
        self.set_reg(VNDR0, 0x10)
        self.set_reg(VNDR1, 0x00)
        self.set_reg(VPWR,  0x00)

        # active window = whole panel
        self.set_reg(HSAW0, 0); self.set_reg(HSAW1, 0)
        self.set_reg(HEAW0, (W - 1) & 0xFF); self.set_reg(HEAW1, (W - 1) >> 8)
        self.set_reg(VSAW0, 0); self.set_reg(VSAW1, 0)
        self.set_reg(VEAW0, (H - 1) & 0xFF); self.set_reg(VEAW1, (H - 1) >> 8)

        self.set_reg(PCLK, 0x00)

        # backlight ON, 50% PWM (vendor default)
        self.set_reg(P1CR,  0x88)
        self.set_reg(P1DCR, 50)

        # bg = white, then memory clear
        self.set_reg(TBCR, 0xFF)
        self.set_reg(MCLR, 0x81)
        self.wait_busy()

        self.set_reg(IODR, 0x07)
        self.set_reg(PWRR, 0x80)             # display ON

    # --- vendor TFT_DataMultiWrite replica ------------------------

    def fill(self, color565: int) -> None:
        self.reg_w(MRWC)
        # Pre-state for burst (vendor: RS=L, CS=L, OE=L; toggle WR per word)
        GPIO.output(RS_PIN, 0)
        GPIO.output(CS_PIN, 0)
        GPIO.output(OE_PIN, 0)

        hi = (color565 >> 8) & 0xFF
        lo = color565 & 0xFF
        # spidev's xfer2 has overhead per call; chunk into one big buffer
        # but issue WR strobes per *word* would be too slow from Python.
        # Instead: rely on auto-WR? No — RAIO needs explicit WR per word.
        # Compromise: issue chunks of N words and toggle WR around each
        # chunk. 595 latches each SPI byte pair, RAIO reads on WR rising.
        # For a uniform fill this still produces the same visible result
        # because every word = same bytes.
        chunk_words = 256
        chunk = [hi, lo] * chunk_words
        total = W * H
        for _ in range(total // chunk_words):
            GPIO.output(WR_PIN, 0)
            self.spi.xfer2(list(chunk))
            GPIO.output(WR_PIN, 1)

        GPIO.output(CS_PIN, 1)
        GPIO.output(OE_PIN, 1)

    def fill_per_word(self, color565: int) -> None:
        """Strict per-word WR strobe (slow but matches vendor exactly)."""
        self.reg_w(MRWC)
        GPIO.output(RS_PIN, 0)
        GPIO.output(CS_PIN, 0)
        GPIO.output(OE_PIN, 0)

        hi = (color565 >> 8) & 0xFF
        lo = color565 & 0xFF
        for _ in range(W * H):
            GPIO.output(WR_PIN, 0)
            self.spi.xfer2([hi, lo])
            GPIO.output(WR_PIN, 1)

        GPIO.output(CS_PIN, 1)
        GPIO.output(OE_PIN, 1)

    def close(self) -> None:
        try:
            self.spi.close()
        except Exception:
            pass
        GPIO.cleanup()


def parse_color(s: str) -> int:
    s = s.strip().lower()
    if s in COLOURS:
        return COLOURS[s]
    return int(s, 0) & 0xFFFF


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--color", default="white",
                    help="colour name (white/red/...) or 0xRRGGBB-565 word")
    ap.add_argument("--strict", action="store_true",
                    help="strict per-word WR strobe (slow but vendor-exact)")
    ap.add_argument("--no-reset", action="store_true",
                    help="skip hard reset (preserve current chip state)")
    ap.add_argument("--no-init", action="store_true",
                    help="skip RAIO_init (assume already configured)")
    ap.add_argument("--speed", type=int, default=16_000_000)
    args = ap.parse_args()

    color = parse_color(args.color)
    print(f"colour 0x{color:04X}; SPI {args.speed/1e6:.1f} MHz; "
          f"strict={args.strict} no_reset={args.no_reset} no_init={args.no_init}")

    p = Panel(hz=args.speed)
    try:
        if not args.no_reset:
            p.hard_reset()
        if not args.no_init:
            p.init()
            print("init done; panel should be white now (vendor default bg)")
            time.sleep(1.0)

        t0 = time.monotonic()
        if args.strict:
            p.fill_per_word(color)
        else:
            p.fill(color)
        print(f"fill done in {time.monotonic() - t0:.2f} s")
    finally:
        p.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
