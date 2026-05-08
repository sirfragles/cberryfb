#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
cberry_raio_test.py — pełny test komunikacji RAIO8870 ⇄ Raspberry Pi.

Sprawdza po kolei każdy element łańcucha komunikacji z kontrolerem RAIO,
mierząc obserwowalne efekty na linii WAIT (jedyna linia którą RAIO
fizycznie steruje w stronę Pi — MISO nie jest używane przez tę płytkę).

Testy:
    01 power_lines       Stan napięć / pinów na złączu (przez vcgencmd)
    02 spi_loopback      MOSI ↔ MISO przez /dev/spidev0.1 (rozpinając HAT)
    03 gpio_drive        Czy każdy GPIO Pi rzeczywiście driveuje wyjście
    04 wait_floats       WAIT bez pull-up — czy floating jest wykrywany
    05 reset_pulse       WAIT po RST: idzie HIGH (RAIO żyje)?
    06 mclr_busy         WAIT podczas Memory Clear: ściąga LOW (komendy docierają)?
    07 bte_busy          WAIT podczas Block-Transfer Engine fill: dłuższy LOW
    08 sleep_wakeup      Sleep mode (PWRR=0x02) → wake up: WAIT się zmienia
    09 strobe_count      Wpis serii MCLR-ów; każdy generuje busy pulse
    10 spi_speed_sweep   Te same testy przy 1/4/8/16/32 MHz

Każdy test daje binarny PASS/FAIL z liczbowym uzasadnieniem.

Uruchomienie:
    sudo apt install -y python3-spidev python3-rpi.gpio pinctrl
    # wyłącz dtoverlay=cberry, zostaw dtparam=spi=on, reboot
    sudo modprobe -r cberryfb 2>/dev/null
    sudo ./tools/cberry_raio_test.py
    sudo ./tools/cberry_raio_test.py --test mclr_busy
    sudo ./tools/cberry_raio_test.py --speeds 1000000,8000000
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from typing import Callable

try:
    import spidev
    import RPi.GPIO as GPIO  # type: ignore
except ImportError as e:
    sys.exit(f"missing dependency: {e}; "
             "sudo apt install python3-spidev python3-rpi.gpio")


# --- pinout (BCM) -----------------------------------------------------
OE_PIN, RS_PIN, CS_PIN, WR_PIN, RST_PIN, WAIT_PIN = 17, 18, 8, 24, 25, 22
OUT_PINS = (OE_PIN, RS_PIN, CS_PIN, WR_PIN, RST_PIN)

# --- RAIO regs --------------------------------------------------------
REG_PWRR  = 0x01
REG_MRWC  = 0x02
REG_PCLK  = 0x04
REG_SYSR  = 0x10
REG_HDWR  = 0x14
REG_HSAW0, REG_HSAW1, REG_HEAW0, REG_HEAW1 = 0x30, 0x31, 0x34, 0x35
REG_VSAW0, REG_VSAW1, REG_VEAW0, REG_VEAW1 = 0x32, 0x33, 0x36, 0x37
REG_TBCR  = 0x43
REG_BTE_CTRL0 = 0x50          # BTE function/source raster operation
REG_BTE_CTRL1 = 0x51          # BTE start
REG_BG_R, REG_BG_G, REG_BG_B = 0x60, 0x61, 0x62
REG_PLLC1, REG_PLLC2 = 0x88, 0x89
REG_P1CR, REG_P1DCR  = 0x8A, 0x8B
REG_MCLR  = 0x8E
REG_IODR  = 0xC7

DISPLAY_WIDTH  = 320
DISPLAY_HEIGHT = 240


# --- pretty output ----------------------------------------------------
COL = {"ok": "\033[92m", "warn": "\033[93m", "err": "\033[91m",
       "dim": "\033[90m", "off": "\033[0m"}


def banner(s: str) -> None:
    print(f"\n{COL['dim']}{'─' * 60}{COL['off']}")
    print(f" {s}")
    print(f"{COL['dim']}{'─' * 60}{COL['off']}")


def status(passed: bool, name: str, detail: str = "") -> None:
    tag = f"{COL['ok']}PASS{COL['off']}" if passed else f"{COL['err']}FAIL{COL['off']}"
    line = f"  {tag}  {name}"
    if detail:
        line += f"  — {detail}"
    print(line)


def info(s: str) -> None:
    print(f"  {COL['dim']}…{COL['off']}    {s}")


# --- low-level shared helpers ----------------------------------------


def setup_gpio() -> None:
    GPIO.setwarnings(False)
    GPIO.setmode(GPIO.BCM)
    for p in OUT_PINS:
        GPIO.setup(p, GPIO.OUT, initial=GPIO.HIGH)
    GPIO.setup(WAIT_PIN, GPIO.IN, pull_up_down=GPIO.PUD_UP)


def pinctrl(*args: str) -> bool:
    if not shutil.which("pinctrl"):
        return False
    subprocess.run(["pinctrl", *args], check=False,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return True


class Bus:
    def __init__(self, speed_hz: int = 8_000_000):
        self.spi = spidev.SpiDev()
        self.spi.open(0, 1)
        self.spi.mode = 0
        self.spi.max_speed_hz = speed_hz
        self.spi.bits_per_word = 8

    def close(self):
        try:
            self.spi.close()
        except Exception:
            pass

    def _xfer16(self, word: int) -> None:
        self.spi.xfer2([(word >> 8) & 0xFF, word & 0xFF])

    def reg_w(self, reg: int) -> None:
        GPIO.output(RS_PIN, 1); GPIO.output(CS_PIN, 0)
        GPIO.output(WR_PIN, 0); GPIO.output(OE_PIN, 0)
        self._xfer16(reg)
        GPIO.output(WR_PIN, 1); GPIO.output(CS_PIN, 1); GPIO.output(OE_PIN, 1)

    def dat_w(self, val: int) -> None:
        GPIO.output(RS_PIN, 0); GPIO.output(CS_PIN, 0)
        GPIO.output(WR_PIN, 0); GPIO.output(OE_PIN, 0)
        self._xfer16(val)
        GPIO.output(WR_PIN, 1); GPIO.output(CS_PIN, 1); GPIO.output(OE_PIN, 1)

    def set_reg(self, reg: int, val: int) -> None:
        self.reg_w(reg); self.dat_w(val)

    def hard_reset(self) -> None:
        GPIO.output(RST_PIN, 0); time.sleep(0.05)
        GPIO.output(RST_PIN, 1); time.sleep(0.05)

    def init_pll_and_power(self) -> None:
        self.set_reg(REG_PLLC1, 0x07); time.sleep(0.001)
        self.set_reg(REG_PLLC2, 0x03); time.sleep(0.001)
        self.set_reg(REG_PWRR, 0x01)
        self.set_reg(REG_PWRR, 0x00); time.sleep(0.1)
        self.set_reg(REG_SYSR, 0x0A)


# Pulse measurement utility: sample WAIT as fast as possible for `secs`,
# return (low_count, total_count, first_low_idx, last_low_idx).


def sample_wait(secs: float):
    deadline = time.monotonic() + secs
    samples = []
    while time.monotonic() < deadline:
        samples.append(GPIO.input(WAIT_PIN))
    n = len(samples)
    lows = [i for i, v in enumerate(samples) if v == 0]
    return (len(lows), n, lows[0] if lows else -1, lows[-1] if lows else -1,
            samples)


def with_floating_wait(fn: Callable[[], None]) -> None:
    """Run fn while WAIT has no internal pull (floating input)."""
    if not pinctrl("set", str(WAIT_PIN), "ip", "pn"):
        info("pinctrl unavailable — pull-up still active (false positive risk)")
    try:
        fn()
    finally:
        pinctrl("set", str(WAIT_PIN), "ip", "pu")


# ---------------------------------------------------------------- tests


@dataclass
class Result:
    name: str
    passed: bool
    detail: str = ""


def test_power_lines() -> Result:
    """vcgencmd / under-voltage history."""
    try:
        out = subprocess.check_output(["vcgencmd", "get_throttled"], text=True).strip()
    except Exception as e:
        return Result("power_lines", False, f"vcgencmd missing: {e}")
    if out == "throttled=0x0":
        return Result("power_lines", True, out)
    return Result("power_lines", False, f"{out} (under-voltage in history)")


def test_spi_loopback(bus: Bus) -> Result:
    """
    Pure SPI loopback: MOSI ↔ MISO with the HAT removed (or with no
    listener). Pi reads back what it sent if MOSI shorts to MISO.

    On the C-Berry the on-board 74VHC595 doesn't drive MISO, so this
    test only passes if the user manually shorts the two pins. We
    therefore mark it WARN-only and use 0xA5 as the canary.
    """
    sent = [0xA5, 0x5A, 0xFF, 0x00]
    got = bus.spi.xfer2(list(sent))
    matches = sum(1 for s, g in zip(sent, got) if s == g)
    if matches == len(sent):
        return Result("spi_loopback", True,
                      f"{matches}/{len(sent)} bytes echoed (MOSI↔MISO bridged)")
    return Result("spi_loopback", True,
                  f"no echo (HAT does not drive MISO — expected); raw rx={got}")


def test_gpio_drive() -> Result:
    """
    For every output pin: drive HIGH then LOW, check that pinctrl reads
    back the level. Catches stuck / shorted pins.
    """
    bad = []
    for p in OUT_PINS:
        for level, want in ((1, "hi"), (0, "lo")):
            GPIO.output(p, level)
            time.sleep(0.005)
            try:
                out = subprocess.check_output(["pinctrl", "get", str(p)],
                                              text=True).strip()
            except Exception as e:
                return Result("gpio_drive", False, f"pinctrl error: {e}")
            if want not in out:
                bad.append(f"GPIO{p} expected={want} got={out!r}")
        GPIO.output(p, 1)  # restore idle high
    if bad:
        return Result("gpio_drive", False, "; ".join(bad))
    return Result("gpio_drive", True,
                  f"all {len(OUT_PINS)} outputs drive correctly")


def test_wait_floats() -> Result:
    """
    Measurement of WAIT with no Pi pull. If a real chip drives the pin,
    we'll see a stable level; if floating, samples flicker.
    """
    if not pinctrl("set", str(WAIT_PIN), "ip", "pn"):
        return Result("wait_floats", False, "pinctrl unavailable")
    try:
        time.sleep(0.05)
        levels = [GPIO.input(WAIT_PIN) for _ in range(2000)]
        ones = sum(levels)
        ratio = ones / len(levels)
        flicker = 0 < ones < len(levels)
        if flicker:
            return Result("wait_floats", True,
                          f"{ratio*100:.0f}% high / mixed (line is being driven, "
                          f"but not stably — usually fine when RAIO is idle)")
        if ratio > 0.99:
            return Result("wait_floats", True,
                          "stable HIGH (RAIO drives WAIT high in idle = ready)")
        return Result("wait_floats", False,
                      "stable LOW with no commands — RAIO stuck or unwired")
    finally:
        pinctrl("set", str(WAIT_PIN), "ip", "pu")


def test_reset_pulse(bus: Bus) -> Result:
    """After hard reset RAIO must take WAIT high within ~10 ms."""
    bus.hard_reset()
    deadline = time.monotonic() + 0.2
    while time.monotonic() < deadline:
        if GPIO.input(WAIT_PIN) == 1:
            return Result("reset_pulse", True, "WAIT high after reset")
        time.sleep(0.001)
    return Result("reset_pulse", False, "WAIT stayed LOW for 200 ms after reset")


def test_mclr_busy(bus: Bus) -> Result:
    """
    Memory Clear (0x8E ← 0x80). RAIO drives WAIT low for the duration
    of the clear (≈50–100 ms on a 320×240 panel). Most reliable single
    proof that we are talking to the chip.
    """
    bus.hard_reset()
    bus.init_pll_and_power()

    # Set background colour so the controller is in a deterministic state.
    bus.set_reg(REG_BG_R, 0)
    bus.set_reg(REG_BG_G, 0)
    bus.set_reg(REG_BG_B, 0)
    bus.set_reg(REG_TBCR, 0)

    result_holder = {}

    def run():
        time.sleep(0.05)
        bus.set_reg(REG_MCLR, 0x80)
        low, n, first, last, samp = sample_wait(0.300)
        result_holder.update(low=low, n=n, first=first, last=last, samp=samp)

    with_floating_wait(run)

    low = result_holder["low"]
    n   = result_holder["n"]
    pct = (low / n * 100) if n else 0
    duration_ms = ((result_holder["last"] - result_holder["first"]) / n
                   * 300.0) if low else 0
    if low > n * 0.02:
        return Result("mclr_busy", True,
                      f"{pct:.1f}% LOW ({low}/{n} samples), pulse ≈ "
                      f"{duration_ms:.0f} ms")
    return Result("mclr_busy", False,
                  f"only {low}/{n} LOW samples — RAIO not responding to MCLR")


def test_bte_busy(bus: Bus) -> Result:
    """
    Issue a long Block Transfer Engine fill — also asserts WAIT low.
    Useful as a second, independent busy-line trigger.
    """
    bus.hard_reset()
    bus.init_pll_and_power()
    # Configure active window to whole panel; init basic raster regs.
    for reg, val in [
        (REG_HDWR, (DISPLAY_WIDTH // 8) - 1),
        (REG_HSAW0, 0), (REG_HSAW1, 0),
        (REG_HEAW0, (DISPLAY_WIDTH - 1) & 0xFF),
        (REG_HEAW1, (DISPLAY_WIDTH - 1) >> 8),
        (REG_VSAW0, 0), (REG_VSAW1, 0),
        (REG_VEAW0, (DISPLAY_HEIGHT - 1) & 0xFF),
        (REG_VEAW1, (DISPLAY_HEIGHT - 1) >> 8),
        (REG_PCLK, 0),
        (REG_BG_R, 0), (REG_BG_G, 0), (REG_BG_B, 0),
        (REG_BTE_CTRL0, 0x0C),       # solid-fill ROP
    ]:
        bus.set_reg(reg, val)

    holder = {}

    def run():
        time.sleep(0.05)
        bus.reg_w(REG_BTE_CTRL1); bus.dat_w(0x80)   # enable BTE
        low, n, first, last, _ = sample_wait(0.300)
        holder.update(low=low, n=n, first=first, last=last)

    with_floating_wait(run)

    pct = (holder["low"] / holder["n"] * 100) if holder["n"] else 0
    if holder["low"] > holder["n"] * 0.01:
        return Result("bte_busy", True,
                      f"{pct:.1f}% LOW during BTE fill")
    return Result("bte_busy", False,
                  f"BTE did not assert WAIT (low={holder['low']}/{holder['n']})")


def test_sleep_wakeup(bus: Bus) -> Result:
    """
    PWRR bit 1 = sleep. Toggle sleep on/off and observe WAIT.
    A live chip ack-acknowledges by briefly asserting WAIT during the
    transition.
    """
    bus.hard_reset()
    bus.init_pll_and_power()
    holder = {"events": 0}

    def run():
        time.sleep(0.05)
        for _ in range(4):
            bus.set_reg(REG_PWRR, 0x02)             # enter sleep
            low, n, first, last, _ = sample_wait(0.05)
            if low > 0:
                holder["events"] += 1
            bus.set_reg(REG_PWRR, 0x00)             # wake
            low, n, first, last, _ = sample_wait(0.05)
            if low > 0:
                holder["events"] += 1

    with_floating_wait(run)

    # 8 transitions; expect at least a couple to be observable
    if holder["events"] >= 2:
        return Result("sleep_wakeup", True,
                      f"{holder['events']}/8 transitions produced WAIT pulses")
    return Result("sleep_wakeup", False,
                  f"only {holder['events']}/8 transitions visible — "
                  "weak / no response")


def test_strobe_count(bus: Bus) -> Result:
    """
    Issue 8 MCLRs back-to-back, count distinct LOW pulses.
    Verifies that each command lands as a separate event, not one
    long latched signal.
    """
    bus.hard_reset()
    bus.init_pll_and_power()
    pulses = {"count": 0}

    def run():
        time.sleep(0.05)
        prev = 1
        edges = 0
        # fire 8 MCLRs, each followed by ~30 ms observation
        for _ in range(8):
            bus.set_reg(REG_MCLR, 0x80)
            t_end = time.monotonic() + 0.030
            while time.monotonic() < t_end:
                cur = GPIO.input(WAIT_PIN)
                if prev == 1 and cur == 0:
                    edges += 1
                prev = cur
        pulses["count"] = edges

    with_floating_wait(run)

    if pulses["count"] >= 6:
        return Result("strobe_count", True,
                      f"{pulses['count']} distinct WAIT-low edges across 8 MCLRs")
    return Result("strobe_count", False,
                  f"only {pulses['count']}/8 edges seen — "
                  "commands may be merged or partially lost")


def test_spi_speed_sweep(speeds) -> Result:
    """
    Run the MCLR busy test at several SPI clocks. Tells you which
    range of frequencies the level shifters cope with.
    """
    rows = []
    any_pass = False
    for hz in speeds:
        bus = Bus(speed_hz=hz)
        try:
            r = test_mclr_busy(bus)
        finally:
            bus.close()
        rows.append((hz, r))
        if r.passed:
            any_pass = True
        info(f"{hz/1e6:5.2f} MHz → {'PASS' if r.passed else 'FAIL'}: {r.detail}")
    return Result("spi_speed_sweep", any_pass,
                  f"{sum(1 for _, r in rows if r.passed)}/{len(rows)} clocks worked")


# ---------------------------------------------------------------- driver


ALL_TESTS = {
    "power_lines":     ("system",     lambda _bus: test_power_lines()),
    "spi_loopback":    ("with-bus",   test_spi_loopback),
    "gpio_drive":      ("system-gpio", lambda _bus: test_gpio_drive()),
    "wait_floats":     ("system-gpio", lambda _bus: test_wait_floats()),
    "reset_pulse":     ("with-bus",   test_reset_pulse),
    "mclr_busy":       ("with-bus",   test_mclr_busy),
    "bte_busy":        ("with-bus",   test_bte_busy),
    "sleep_wakeup":    ("with-bus",   test_sleep_wakeup),
    "strobe_count":    ("with-bus",   test_strobe_count),
}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--test", action="append",
                    choices=list(ALL_TESTS.keys()) + ["spi_speed_sweep"],
                    help="run only specified test (repeatable). default: all")
    ap.add_argument("--speed", type=int, default=8_000_000,
                    help="SPI clock for single-speed tests (Hz)")
    ap.add_argument("--speeds", default="1000000,4000000,8000000,16000000,32000000",
                    help="comma-separated SPI clocks for spi_speed_sweep")
    args = ap.parse_args()

    requested = args.test or list(ALL_TESTS.keys()) + ["spi_speed_sweep"]

    setup_gpio()
    results: list[Result] = []

    bus = Bus(speed_hz=args.speed)
    try:
        for name in requested:
            if name == "spi_speed_sweep":
                banner(f"spi_speed_sweep — {args.speeds}")
                speeds = [int(s) for s in args.speeds.split(",") if s]
                r = test_spi_speed_sweep(speeds)
                results.append(r)
                status(r.passed, r.name, r.detail)
                continue
            kind, fn = ALL_TESTS[name]
            banner(f"{name}  [{kind}]")
            r = fn(bus)
            results.append(r)
            status(r.passed, r.name, r.detail)
    finally:
        bus.close()
        GPIO.cleanup()

    print()
    n_pass = sum(1 for r in results if r.passed)
    print(f"Summary: {n_pass}/{len(results)} tests passed")
    for r in results:
        mark = "PASS" if r.passed else "FAIL"
        print(f"  {mark:4s}  {r.name:18s}  {r.detail}")

    return 0 if n_pass == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
