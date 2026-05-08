#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
cberry_mclr_color.py — test if our init alone is enough.

Strategy: do hard reset + full init, then write background colour
registers (BGCR0/1/2) and trigger Memory Clear with bg colour.
RAIO clears its RAM internally — NO SPI pixel burst from us.

If screen turns RED → init is correct, bug is in our data-burst.
If screen stays dark/wrong → init itself is broken; we'll iterate.

Pre-conditions:
    sudo modprobe -r cberryfb
    overlay disabled (dtoverlay=cberry commented out, after reboot)

Usage:
    sudo ./tools/cberry_mclr_color.py            # red
    sudo ./tools/cberry_mclr_color.py --color blue
"""

from __future__ import annotations
import argparse
import sys
import time

import spidev
import RPi.GPIO as GPIO  # type: ignore

OE_PIN, RS_PIN, CS_PIN, WR_PIN, RD_PIN, RST_PIN, WAIT_PIN = 17, 18, 8, 24, 23, 25, 22
W, H = 320, 240

PWRR, MRWC, PCLK, SYSR, HDWR = 0x01, 0x02, 0x04, 0x10, 0x14
HNDFTR, HNDR, HSTR, HPWR     = 0x15, 0x16, 0x17, 0x18
VDHR0, VDHR1, VNDR0, VNDR1, VPWR = 0x19, 0x1A, 0x1B, 0x1C, 0x1F
HSAW0, HSAW1, HEAW0, HEAW1 = 0x30, 0x31, 0x34, 0x35
VSAW0, VSAW1, VEAW0, VEAW1 = 0x32, 0x33, 0x36, 0x37
TBCR, MCLR_REG = 0x43, 0x8E
PLLC1, PLLC2, P1CR, P1DCR = 0x88, 0x89, 0x8A, 0x8B
IODR = 0x13
DPCR = 0x20

# Background colour registers for 65k mode
BGCR0_R, BGCR1_G, BGCR2_B = 0x60, 0x61, 0x62

NAMED = {
    "black":  (0,   0,   0),
    "white":  (255, 255, 255),
    "red":    (255, 0,   0),
    "green":  (0,   255, 0),
    "blue":   (0,   0,   255),
    "yellow": (255, 255, 0),
    "cyan":   (0,   255, 255),
    "magenta":(255, 0,   255),
}


def setup() -> spidev.SpiDev:
    GPIO.setwarnings(False)
    GPIO.setmode(GPIO.BCM)
    for p in (OE_PIN, RS_PIN, CS_PIN, WR_PIN, RD_PIN, RST_PIN):
        GPIO.setup(p, GPIO.OUT, initial=GPIO.HIGH)
    GPIO.setup(WAIT_PIN, GPIO.IN, pull_up_down=GPIO.PUD_UP)
    spi = spidev.SpiDev()
    spi.open(0, 1)
    spi.mode = 0
    spi.max_speed_hz = 8_000_000
    spi.bits_per_word = 8
    return spi


def xfer16(spi, w):
    spi.xfer2([(w >> 8) & 0xFF, w & 0xFF])


def reg_w(spi, reg):
    GPIO.output(RS_PIN, 1); GPIO.output(CS_PIN, 0)
    GPIO.output(WR_PIN, 0); GPIO.output(OE_PIN, 0)
    xfer16(spi, reg)
    GPIO.output(WR_PIN, 1); GPIO.output(CS_PIN, 1); GPIO.output(OE_PIN, 1)


def dat_w(spi, val):
    GPIO.output(RS_PIN, 0); GPIO.output(CS_PIN, 0)
    GPIO.output(WR_PIN, 0); GPIO.output(OE_PIN, 0)
    xfer16(spi, val)
    GPIO.output(WR_PIN, 1); GPIO.output(CS_PIN, 1); GPIO.output(OE_PIN, 1)


def set_reg(spi, reg, val):
    reg_w(spi, reg)
    dat_w(spi, val)


def wait_busy(secs=1.0):
    deadline = time.monotonic() + secs
    while time.monotonic() < deadline and GPIO.input(WAIT_PIN) == 0:
        pass


def hard_reset():
    GPIO.output(RST_PIN, 0); time.sleep(0.05)
    GPIO.output(RST_PIN, 1); time.sleep(0.05)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--color", default="red")
    args = ap.parse_args()

    name = args.color.lower()
    if name not in NAMED:
        sys.exit(f"unknown colour {args.color}; choose: {list(NAMED)}")
    r, g, b = NAMED[name]
    print(f"target colour: {name} (R={r} G={g} B={b})")

    spi = setup()
    try:
        hard_reset()
        print("== init (vendor 1:1) ==")

        # PLL
        set_reg(spi, PLLC1, 0x07); time.sleep(200e-6)
        set_reg(spi, PLLC2, 0x03); time.sleep(200e-6)

        # software reset of RAIO
        set_reg(spi, PWRR, 0x01)
        set_reg(spi, PWRR, 0x00)
        time.sleep(0.1)

        # color mode 65k, single layer
        set_reg(spi, SYSR, 0x0A)
        set_reg(spi, DPCR, 0x00)

        # horizontal
        set_reg(spi, HDWR,   (W // 8) - 1)
        set_reg(spi, HNDFTR, 0x02)
        set_reg(spi, HNDR,   0x03)
        set_reg(spi, HSTR,   0x04)
        set_reg(spi, HPWR,   0x03)

        # vertical
        set_reg(spi, VDHR0, (H - 1) & 0xFF)
        set_reg(spi, VDHR1, (H - 1) >> 8)
        set_reg(spi, VNDR0, 0x10)
        set_reg(spi, VNDR1, 0x00)
        set_reg(spi, VPWR,  0x00)

        # active window = whole panel
        set_reg(spi, HSAW0, 0); set_reg(spi, HSAW1, 0)
        set_reg(spi, HEAW0, (W - 1) & 0xFF); set_reg(spi, HEAW1, (W - 1) >> 8)
        set_reg(spi, VSAW0, 0); set_reg(spi, VSAW1, 0)
        set_reg(spi, VEAW0, (H - 1) & 0xFF); set_reg(spi, VEAW1, (H - 1) >> 8)

        # PCLK rising edge
        set_reg(spi, PCLK, 0x00)

        # backlight 50% (vendor default)
        set_reg(spi, P1CR,  0x88)
        set_reg(spi, P1DCR, 50)

        # set bg colour AND text bg colour (some firmware uses TBCR for clear)
        set_reg(spi, BGCR0_R, r >> 3)        # 5 bits R
        set_reg(spi, BGCR1_G, g >> 2)        # 6 bits G
        set_reg(spi, BGCR2_B, b >> 3)        # 5 bits B
        set_reg(spi, TBCR, 0xFF)             # text bg = white (vendor sets this)

        # MCLR with active window (vendor uses 0x81 here)
        set_reg(spi, MCLR_REG, 0x81)
        wait_busy(2.0)

        set_reg(spi, IODR, 0x07)             # NOW correctly hits register 0x13
        set_reg(spi, PWRR, 0x80)             # display ON

        print("done — screen should be solid", name)
        time.sleep(3)
    finally:
        spi.close()
        GPIO.cleanup()


if __name__ == "__main__":
    main()
