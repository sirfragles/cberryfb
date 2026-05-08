#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
cberry_min_backlight.py — absolutnie minimalny test podświetlenia C-Berry.

Robi tylko: hard reset → włącz PLL → włącz zasilanie wyświetlacza →
przełącza P1DCR (PWM backlight) między 0 a 0xFF kilka razy.

Używać po pomyślnym `cberry_diag.py --step handshake`. Jeśli mimo
działającej komunikacji RAIO panel nie reaguje wzrokowo na ten skrypt,
to znaczy że LED backlightu lub jego driver na płytce HAT są martwe —
nie problem ze sterownikiem.

Wymaga `dtparam=spi=on` i wyłączonego `dtoverlay=cberry`
(patrz tools/README.md).
"""

import sys
import time

try:
    import spidev
    import RPi.GPIO as GPIO  # type: ignore
except ImportError as e:
    sys.exit(f"missing dependency: {e}; "
             "sudo apt install python3-spidev python3-rpi.gpio")


OE, RS, CS, WR, RST, WAIT = 17, 18, 8, 24, 25, 22


def setup_gpio() -> None:
    GPIO.setwarnings(False)
    GPIO.setmode(GPIO.BCM)
    for p in (OE, RS, CS, WR, RST):
        GPIO.setup(p, GPIO.OUT, initial=GPIO.HIGH)
    GPIO.setup(WAIT, GPIO.IN, pull_up_down=GPIO.PUD_UP)


def write_pair(spi: spidev.SpiDev, reg: int, val: int) -> None:
    # register
    GPIO.output(RS, 1); GPIO.output(CS, 0); GPIO.output(WR, 0); GPIO.output(OE, 0)
    spi.xfer2([(reg >> 8) & 0xFF, reg & 0xFF])
    GPIO.output(WR, 1); GPIO.output(CS, 1); GPIO.output(OE, 1)
    # data
    GPIO.output(RS, 0); GPIO.output(CS, 0); GPIO.output(WR, 0); GPIO.output(OE, 0)
    spi.xfer2([(val >> 8) & 0xFF, val & 0xFF])
    GPIO.output(WR, 1); GPIO.output(CS, 1); GPIO.output(OE, 1)


def main() -> int:
    setup_gpio()
    spi = spidev.SpiDev()
    spi.open(0, 1)
    spi.mode = 0
    spi.max_speed_hz = 8_000_000

    # hard reset
    GPIO.output(RST, 0); time.sleep(0.05)
    GPIO.output(RST, 1); time.sleep(0.05)

    # absolute minimum to make PWM1 fire backlight
    write_pair(spi, 0x88, 0x07); time.sleep(0.001)   # PLLC1
    write_pair(spi, 0x89, 0x03); time.sleep(0.001)   # PLLC2
    write_pair(spi, 0x01, 0x00); time.sleep(0.1)     # PWRR power on
    write_pair(spi, 0x10, 0x0A)                      # SYSR 16bpp
    write_pair(spi, 0x01, 0x80)                      # PWRR display on

    write_pair(spi, 0x8A, 0x88)                      # P1CR enable PWM1 /256

    print("Patrz na panel — pętla OFF/ON x3, każdy stan 1 s")
    for i in range(3):
        write_pair(spi, 0x8B, 0x00)
        print(f"  [{i+1}/3] OFF  (P1DCR=0x00)")
        time.sleep(1.0)
        write_pair(spi, 0x8B, 0xFF)
        print(f"  [{i+1}/3] FULL (P1DCR=0xFF)")
        time.sleep(1.0)

    spi.close()
    GPIO.cleanup()
    return 0


if __name__ == "__main__":
    sys.exit(main())
