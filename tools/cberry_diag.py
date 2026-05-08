#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
cberry_diag.py — standalone hardware diagnostic for the admatec C-Berry
3.5" RAIO8870 LCD HAT.

Run on a Raspberry Pi *without* the cberryfb kernel module. The script
talks to the panel directly through /dev/spidev0.1 and the bcm2835
GPIO chip, mirroring the original vendor C SDK (C-Berry/SW/tft_test).

Use it to localise where the chain breaks:
    1. is the HAT powered                  (GPIO levels, WAIT pin)
    2. does SPI reach the level shifter    (SPI loopback / register reads)
    3. does the RAIO8870 ack commands      (status register, busy line)
    4. does the backlight respond          (P1CR / P1DCR PWM regs)
    5. does the panel actually show pixels (full-frame fills, ramps)

Pre-requisites on the Pi:
    sudo apt install -y python3-spidev python3-rpi.gpio python3-numpy
    # comment out 'dtoverlay=cberry' in /boot/firmware/config.txt and
    # reboot, so spidev0.1 + bare GPIOs are available
    sudo modprobe -r cberryfb 2>/dev/null   # (in case it auto-loaded)

Usage:
    sudo ./tools/cberry_diag.py                 # full self-test
    sudo ./tools/cberry_diag.py --step gpios    # only one phase
    sudo ./tools/cberry_diag.py --speed 4000000 # try a different SPI clock
    sudo ./tools/cberry_diag.py --pattern bars  # show colour bars
"""

from __future__ import annotations

import argparse
import sys
import time
from contextlib import contextmanager

try:
    import spidev
except ImportError:
    sys.exit("missing python3-spidev: sudo apt install python3-spidev")

try:
    import RPi.GPIO as GPIO  # type: ignore
except ImportError:
    sys.exit("missing python3-rpi.gpio: sudo apt install python3-rpi.gpio")


# --- pin map (BCM numbering, matches cberry.dts and the vendor SDK) ---
OE_PIN   = 17
RS_PIN   = 18
CS_PIN   = 8
WR_PIN   = 24
RST_PIN  = 25
WAIT_PIN = 22

OUT_PINS = (OE_PIN, RS_PIN, CS_PIN, WR_PIN, RST_PIN)

DISPLAY_WIDTH  = 320
DISPLAY_HEIGHT = 240

# RAIO8870 registers (subset, see RAIO8870.h)
REG_PWRR  = 0x01
REG_MRWC  = 0x02
REG_PCLK  = 0x04
REG_SYSR  = 0x10
REG_HDWR  = 0x14
REG_HNDFTR= 0x15
REG_HNDR  = 0x16
REG_HSTR  = 0x17
REG_HPWR  = 0x18
REG_VDHR0 = 0x19
REG_VDHR1 = 0x1A
REG_VNDR0 = 0x1B
REG_VNDR1 = 0x1C
REG_VPWR  = 0x1D
REG_DPCR  = 0x20
REG_HSAW0 = 0x30
REG_HSAW1 = 0x31
REG_VSAW0 = 0x32
REG_VSAW1 = 0x33
REG_HEAW0 = 0x34
REG_HEAW1 = 0x35
REG_VEAW0 = 0x36
REG_VEAW1 = 0x37
REG_TBCR  = 0x43
REG_PLLC1 = 0x88
REG_PLLC2 = 0x89
REG_P1CR  = 0x8A
REG_P1DCR = 0x8B
REG_MCLR  = 0x8E
REG_IODR  = 0xC7

# ---------------------------------------------------------------- helpers


class Panel:
    def __init__(self, speed_hz: int = 8_000_000) -> None:
        self.spi = spidev.SpiDev()
        self.spi.open(0, 1)            # /dev/spidev0.1
        self.spi.mode = 0
        self.spi.max_speed_hz = speed_hz
        self.spi.bits_per_word = 8

    def close(self) -> None:
        try:
            self.spi.close()
        except Exception:
            pass

    # --- low-level bus ------------------------------------------------

    def _write_word(self, word: int) -> None:
        self.spi.xfer2([(word >> 8) & 0xFF, word & 0xFF])

    def _idle(self) -> None:
        GPIO.output(OE_PIN, 1)
        GPIO.output(CS_PIN, 1)
        GPIO.output(WR_PIN, 1)
        GPIO.output(RS_PIN, 1)

    def reg_write(self, reg: int) -> None:
        GPIO.output(RS_PIN, 1)
        GPIO.output(CS_PIN, 0)
        GPIO.output(WR_PIN, 0)
        GPIO.output(OE_PIN, 0)
        self._write_word(reg & 0xFFFF)
        GPIO.output(WR_PIN, 1)
        GPIO.output(CS_PIN, 1)
        GPIO.output(OE_PIN, 1)

    def dat_write(self, val: int) -> None:
        GPIO.output(RS_PIN, 0)
        GPIO.output(CS_PIN, 0)
        GPIO.output(WR_PIN, 0)
        GPIO.output(OE_PIN, 0)
        self._write_word(val & 0xFFFF)
        GPIO.output(WR_PIN, 1)
        GPIO.output(CS_PIN, 1)
        GPIO.output(OE_PIN, 1)

    def set_reg(self, reg: int, val: int) -> None:
        self.reg_write(reg)
        self.dat_write(val)

    def hard_reset(self) -> None:
        GPIO.output(RST_PIN, 0)
        time.sleep(0.01)
        GPIO.output(RST_PIN, 1)
        time.sleep(0.01)

    def wait_busy(self, timeout_s: float = 0.5) -> bool:
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            if GPIO.input(WAIT_PIN):
                return True
            time.sleep(0.001)
        return False

    # --- bring-up -----------------------------------------------------

    def init_panel(self) -> None:
        self.set_reg(REG_PLLC1, 0x07); time.sleep(0.001)
        self.set_reg(REG_PLLC2, 0x03); time.sleep(0.001)
        self.set_reg(REG_PWRR,  0x01)
        self.set_reg(REG_PWRR,  0x00); time.sleep(0.1)

        self.set_reg(REG_SYSR,  0x0A)
        self.set_reg(REG_DPCR,  0x00)

        self.set_reg(REG_HDWR,   (DISPLAY_WIDTH // 8) - 1)
        self.set_reg(REG_HNDFTR, 0x02)
        self.set_reg(REG_HNDR,   0x03)
        self.set_reg(REG_HSTR,   0x04)
        self.set_reg(REG_HPWR,   0x03)

        self.set_reg(REG_VDHR0, (DISPLAY_HEIGHT - 1) & 0xFF)
        self.set_reg(REG_VDHR1, (DISPLAY_HEIGHT - 1) >> 8)
        self.set_reg(REG_VNDR0, 0x10)
        self.set_reg(REG_VNDR1, 0x00)
        self.set_reg(REG_VPWR,  0x00)

        self.set_reg(REG_HSAW0, 0)
        self.set_reg(REG_HSAW1, 0)
        self.set_reg(REG_HEAW0, (DISPLAY_WIDTH - 1) & 0xFF)
        self.set_reg(REG_HEAW1, (DISPLAY_WIDTH - 1) >> 8)
        self.set_reg(REG_VSAW0, 0)
        self.set_reg(REG_VSAW1, 0)
        self.set_reg(REG_VEAW0, (DISPLAY_HEIGHT - 1) & 0xFF)
        self.set_reg(REG_VEAW1, (DISPLAY_HEIGHT - 1) >> 8)

        self.set_reg(REG_PCLK, 0x00)

        self.set_reg(REG_P1CR,  0x88)
        self.set_reg(REG_P1DCR, 0xFF)

        self.set_reg(REG_TBCR, 0x00)
        self.set_reg(REG_MCLR, 0x81)
        self.wait_busy()

        self.set_reg(REG_IODR, 0x07)
        self.set_reg(REG_PWRR, 0x80)

    def set_backlight(self, value: int) -> None:
        value = max(0, min(255, int(value)))
        self.set_reg(REG_P1CR,  0x88)
        self.set_reg(REG_P1DCR, value)

    # --- pixel push ---------------------------------------------------

    def fill_rgb565(self, color: int) -> None:
        self.reg_write(REG_MRWC)
        GPIO.output(RS_PIN, 0)
        GPIO.output(CS_PIN, 0)
        GPIO.output(OE_PIN, 0)
        hi, lo = (color >> 8) & 0xFF, color & 0xFF
        # do it in chunks to keep xfer2 happy
        chunk = bytes([hi, lo]) * 256          # 512-byte buffers
        full = chunk * (DISPLAY_WIDTH * DISPLAY_HEIGHT // 256)
        for i in range(0, len(full), 4096):
            GPIO.output(WR_PIN, 1)
            self.spi.xfer2(list(full[i:i + 4096]))
            GPIO.output(WR_PIN, 0)
        GPIO.output(WR_PIN, 1)
        GPIO.output(CS_PIN, 1)
        GPIO.output(OE_PIN, 1)

    def colour_bars(self) -> None:
        bars = [0xF800, 0x07E0, 0x001F, 0xFFE0, 0xF81F, 0x07FF, 0xFFFF, 0x0000]
        bar_w = DISPLAY_WIDTH // len(bars)
        self.reg_write(REG_MRWC)
        GPIO.output(RS_PIN, 0)
        GPIO.output(CS_PIN, 0)
        GPIO.output(OE_PIN, 0)
        for _ in range(DISPLAY_HEIGHT):
            for i, c in enumerate(bars):
                hi, lo = (c >> 8) & 0xFF, c & 0xFF
                buf = bytes([hi, lo]) * bar_w
                GPIO.output(WR_PIN, 1)
                self.spi.xfer2(list(buf))
                GPIO.output(WR_PIN, 0)
        GPIO.output(WR_PIN, 1)
        GPIO.output(CS_PIN, 1)
        GPIO.output(OE_PIN, 1)


# --- pretty output ----------------------------------------------------


COL = {"ok": "\033[92m", "warn": "\033[93m", "err": "\033[91m", "off": "\033[0m"}


def step(label: str) -> None:
    print(f"\n=== {label} ===")


def ok(msg: str) -> None:
    print(f"  {COL['ok']}OK{COL['off']}    {msg}")


def warn(msg: str) -> None:
    print(f"  {COL['warn']}WARN{COL['off']}  {msg}")


def fail(msg: str) -> None:
    print(f"  {COL['err']}FAIL{COL['off']}  {msg}")


# --- diagnostic phases ------------------------------------------------


def phase_env() -> None:
    step("Environment")
    import os, subprocess
    if os.geteuid() != 0:
        warn("not running as root; SPI/GPIO access may fail")

    try:
        out = subprocess.check_output(["vcgencmd", "get_throttled"], text=True).strip()
        if out == "throttled=0x0":
            ok(out)
        else:
            warn(out + " (under-voltage / throttle in history)")
    except Exception as e:
        warn(f"vcgencmd not available: {e}")

    try:
        out = subprocess.check_output(["lsmod"], text=True)
        if "cberryfb" in out:
            fail("cberryfb module is loaded — unload it: sudo modprobe -r cberryfb")
        else:
            ok("cberryfb module is NOT loaded")
    except Exception as e:
        warn(f"lsmod failed: {e}")

    try:
        with open("/sys/firmware/devicetree/base/model") as f:
            ok(f"board: {f.read().strip()}")
    except Exception:
        pass


def phase_spidev() -> bool:
    step("/dev/spidev presence")
    import os
    have01 = os.path.exists("/dev/spidev0.1")
    if have01:
        ok("/dev/spidev0.1 exists")
    else:
        fail("/dev/spidev0.1 missing — check 'dtparam=spi=on' and disable cberry overlay")
    return have01


def phase_gpios(panel: Panel) -> None:
    step("GPIO pre-flight (idle levels)")
    panel._idle()
    time.sleep(0.05)
    levels = {
        "OE":   GPIO.input(OE_PIN),
        "RS":   GPIO.input(RS_PIN),
        "CS":   GPIO.input(CS_PIN),
        "WR":   GPIO.input(WR_PIN),
        "RST":  GPIO.input(RST_PIN),
        "WAIT": GPIO.input(WAIT_PIN),
    }
    for k, v in levels.items():
        marker = ok if v in (0, 1) else warn
        marker(f"{k:5s} = {v}")
    if levels["WAIT"] == 0:
        warn("WAIT is LOW with pull-up enabled → either the line is shorted to "
             "GND or the HAT is not powered (check 3.3V/5V on the header).")


def phase_reset(panel: Panel) -> None:
    step("Hard reset + WAIT poll")
    panel.hard_reset()
    if panel.wait_busy(timeout_s=0.2):
        ok("RAIO WAIT went high after reset (chip is responsive)")
    else:
        fail("RAIO WAIT stayed low — chip not responding (power / wiring)")


def phase_handshake(panel: Panel) -> None:
    """
    Definitive test that the RAIO actually receives our commands.

    The pull-up on WAIT will read HIGH whether or not the chip is
    present, so the only honest test is to make RAIO drive WAIT LOW
    and watch it from a *floating* input. The MCLR command (0x8E with
    bit7=1) starts an internal memory-clear cycle that holds WAIT low
    for ~50-100 ms.

    1. Disable internal pull on GPIO22, switch it to floating input.
    2. Read baseline level a few times.
    3. Issue MCLR=0x80, immediately sample WAIT for 200 ms.
    4. If we observe a LOW pulse — RAIO is *definitely* listening.
       If WAIT never drops — communication is dead even though the
       Pi side looks fine.
    """
    step("Communication handshake (MCLR busy pulse on floating WAIT)")

    # Re-program WAIT pin without internal pull. RPi.GPIO doesn't expose
    # PUD_OFF reliably across versions, so use raspi-gpio if available.
    import subprocess, shutil
    if shutil.which("raspi-gpio"):
        subprocess.run(["raspi-gpio", "set", str(WAIT_PIN), "ip", "pn"],
                       check=False)
        ok("set GPIO22 to input, no pull (raspi-gpio)")
    else:
        warn("raspi-gpio not found; pull-up still active, "
             "test result may be a false positive")

    # baseline
    base_high = sum(GPIO.input(WAIT_PIN) for _ in range(20)) / 20.0
    print(f"  WAIT baseline (no command): {base_high*100:.0f}% high "
          f"over 20 samples")

    # fire MCLR and sample as fast as we can for 200 ms
    panel.set_reg(REG_MCLR, 0x80)
    deadline = time.monotonic() + 0.2
    samples = []
    while time.monotonic() < deadline:
        samples.append(GPIO.input(WAIT_PIN))

    low_count = samples.count(0)
    pct_low = 100.0 * low_count / max(1, len(samples))
    print(f"  WAIT during MCLR: {pct_low:.0f}% low across "
          f"{len(samples)} samples")

    if low_count > len(samples) * 0.05:
        ok("WAIT was driven LOW by RAIO during MCLR — communication "
           "is REAL (chip listens, busy-line works)")
    else:
        fail("WAIT never went low during MCLR — RAIO is NOT receiving "
             "our writes (despite passing earlier checks). Likely: "
             "level shifter / 5V rail / FFC seating / dead chip.")

    # restore pull-up for the rest of the script
    if shutil.which("raspi-gpio"):
        subprocess.run(["raspi-gpio", "set", str(WAIT_PIN), "ip", "pu"],
                       check=False)


def phase_backlight(panel: Panel) -> None:
    step("Backlight ramp test (look at the panel!)")
    print("  Setting backlight 0 → 255 → 0 → 255. Each step ~0.5 s.")
    for v in (0, 64, 128, 192, 255, 0, 255):
        panel.set_backlight(v)
        print(f"  P1DCR = {v:3d}")
        time.sleep(0.5)
    print("  Did the LCD backlight visibly change brightness? (y/n)")


def phase_init(panel: Panel) -> None:
    step("Panel init (RAIO sequence)")
    panel.init_panel()
    if panel.wait_busy(timeout_s=1.0):
        ok("init completed, WAIT high")
    else:
        warn("WAIT still low after init — controller may be stuck")


def phase_pattern(panel: Panel, kind: str) -> None:
    step(f"Pattern: {kind}")
    if kind == "bars":
        panel.colour_bars()
    elif kind == "red":
        panel.fill_rgb565(0xF800)
    elif kind == "green":
        panel.fill_rgb565(0x07E0)
    elif kind == "blue":
        panel.fill_rgb565(0x001F)
    elif kind == "white":
        panel.fill_rgb565(0xFFFF)
    elif kind == "black":
        panel.fill_rgb565(0x0000)
    elif kind == "cycle":
        for c, name in [(0xF800, "red"), (0x07E0, "green"), (0x001F, "blue"),
                        (0xFFFF, "white"), (0x0000, "black")]:
            print(f"  fill {name} (0x{c:04x})")
            panel.fill_rgb565(c)
            time.sleep(1.0)
    else:
        warn(f"unknown pattern '{kind}'")


# --- entry point ------------------------------------------------------


def setup_gpio() -> None:
    GPIO.setwarnings(False)
    GPIO.setmode(GPIO.BCM)
    for p in OUT_PINS:
        GPIO.setup(p, GPIO.OUT, initial=GPIO.HIGH)
    GPIO.setup(WAIT_PIN, GPIO.IN, pull_up_down=GPIO.PUD_UP)


@contextmanager
def panel_session(speed_hz: int):
    setup_gpio()
    panel = Panel(speed_hz=speed_hz)
    try:
        yield panel
    finally:
        panel.close()
        GPIO.cleanup()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--speed", type=int, default=8_000_000,
                    help="SPI clock in Hz (default 8 MHz)")
    ap.add_argument("--step", choices=["env", "spidev", "gpios", "reset",
                                       "handshake",
                                       "backlight", "init", "pattern", "all"],
                    default="all")
    ap.add_argument("--pattern", default="cycle",
                    choices=["bars", "red", "green", "blue",
                             "white", "black", "cycle"],
                    help="when --step pattern, which pattern to draw")
    args = ap.parse_args()

    print(f"cberry_diag — SPI clock = {args.speed/1e6:.2f} MHz")

    if args.step in ("env", "all"):
        phase_env()
    if args.step in ("spidev", "all"):
        if not phase_spidev() and args.step == "spidev":
            return 1

    if args.step == "spidev":
        return 0
    if args.step == "env":
        return 0

    with panel_session(args.speed) as panel:
        if args.step in ("gpios", "all"):
            phase_gpios(panel)
        if args.step in ("reset", "all"):
            phase_reset(panel)
        if args.step in ("init", "all"):
            phase_init(panel)
        if args.step in ("handshake", "all"):
            phase_handshake(panel)
        if args.step in ("backlight", "all"):
            phase_backlight(panel)
        if args.step in ("pattern", "all"):
            phase_pattern(panel, args.pattern)

    print("\nDone. If nothing lit up: power / wiring problem.")
    print("If backlight worked but pixels are wrong: timing / strobe issue in driver.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
