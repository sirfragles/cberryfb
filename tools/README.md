# tools/ — userspace helpers for the C-Berry LCD

All scripts assume the `cberryfb` kernel module is loaded and the panel
is exposed as `/dev/fb1` plus `/sys/class/backlight/cberryfb/`.

| Script                | Purpose                                                          |
|-----------------------|------------------------------------------------------------------|
| `cberry_backlight.sh` | Set backlight (0–255) via sysfs.                                 |
| `cberry_show.sh`      | Display a single image file (uses fbi).                          |
| `cberry_console.sh`   | Run a real terminal (fbterm) directly on the C-Berry LCD.        |
| `cberry_xmonitor.sh`  | Make the C-Berry the system's primary X display (Xorg fbdev).    |

## Quick reference

```bash
# Backlight (0..255)
sudo ./cberry_backlight.sh 200

# Show a picture
sudo ./cberry_show.sh /path/to/image.jpg

# Console on the panel (one-shot, foreground)
sudo ./cberry_console.sh run

# Console on the panel as a persistent service
sudo ./cberry_console.sh install
sudo ./cberry_console.sh remove

# Make the panel the only X display (kiosk-style; reboot)
sudo ./cberry_xmonitor.sh install
sudo reboot
```

## Notes

- 320×240 is tiny; for the X path pair it with a kiosk shell
  (`matchbox-window-manager` + a single fullscreen app).
- The previous `cberry_mirror.sh` (raspi2fb / DispmanX) was removed
  because the legacy VC4 userland was dropped from 64-bit Bookworm.
  Use `cberry_console.sh` for a terminal, or `cberry_xmonitor.sh` for X.
