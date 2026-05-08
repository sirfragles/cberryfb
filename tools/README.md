# tools/ — userspace helpers for the C-Berry LCD

All scripts assume the `cberryfb` kernel module is loaded and the panel
is exposed as `/dev/fb1` plus `/sys/class/backlight/cberryfb/`.

| Script                | Purpose                                                      |
|-----------------------|--------------------------------------------------------------|
| `cberry_backlight.sh` | Set backlight (0–255) via sysfs.                             |
| `cberry_show.sh`      | Display a single image file (uses fbi).                      |
| `cberry_mirror.sh`    | Install/remove a systemd service that mirrors HDMI → C-Berry.|
| `cberry_xmonitor.sh`  | Make the C-Berry the system's primary X display (Xorg fbdev).|

## Quick reference

```bash
# Backlight (0..255)
sudo ./cberry_backlight.sh 200

# Show a picture
sudo ./cberry_show.sh /path/to/image.jpg

# Mirror HDMI console onto the panel (run once, then reboot)
sudo ./cberry_mirror.sh install
sudo systemctl set-default multi-user.target
sudo reboot

# Make the panel the only X display (kiosk-style; reboot)
sudo ./cberry_xmonitor.sh install
sudo reboot
```
