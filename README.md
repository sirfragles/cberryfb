cberryfb
========

Linux framebuffer driver for the admatec C-Berry 320×240 LCD.

> Sterownik został przepisany pod współczesne API jądra (≥ 6.6):
> jest `spi_driver`em wiązanym z urządzeniem przez **Device Tree**,
> używa GPIO descriptors (`gpiod`) i nowych helperów framebuffera
> dla pamięci systemowej (`FB_GEN_DEFAULT_DEFERRED_SYSMEM_OPS`).
> Działa przenośnie na **Raspberry Pi 1 – Pi 5** (32-bit i 64-bit).


## Instalacja (natywnie na Raspberry Pi)

Instrukcja dla Raspberry Pi OS **Bookworm** (jądro 6.x).
Wszystkie polecenia wykonujemy bezpośrednio na Pi.

### 1. Zależności

```bash
sudo apt update
sudo apt install -y build-essential device-tree-compiler git bc
```

Następnie nagłówki jądra. Na **Raspberry Pi OS Bookworm i nowszych**
nie istnieje już pakiet `raspberrypi-kernel-headers` — zamiast tego są
metapakiety `linux-headers-rpi-*` zależne od wariantu jądra:

| `uname -r` (przykład)       | Pakiet                  |
|-----------------------------|-------------------------|
| `*-v6+`   (Pi 1 / Zero)     | `linux-headers-rpi-v6`  |
| `*-v7+`   (Pi 2 / 3 / Z2W 32-bit) | `linux-headers-rpi-v7`  |
| `*-v7l+`  (Pi 4 32-bit)     | `linux-headers-rpi-v7l` |
| `*-v8+`   (Pi 3/4/Z2W 64-bit) | `linux-headers-rpi-v8`  |
| `*-2712`  (Pi 5)            | `linux-headers-rpi-2712`|

Auto-wybór odpowiedniego pakietu:

```bash
KREL=$(uname -r)
case "$KREL" in
  *-2712)  PKG=linux-headers-rpi-2712 ;;
  *-v8+)   PKG=linux-headers-rpi-v8   ;;
  *-v7l+)  PKG=linux-headers-rpi-v7l  ;;
  *-v7+)   PKG=linux-headers-rpi-v7   ;;
  *-v6+)   PKG=linux-headers-rpi-v6   ;;
  *)       PKG=linux-headers-$KREL    ;;  # fallback (np. mainline)
esac
sudo apt install -y "$PKG"
```

> Na starszych systemach (Bullseye i wcześniej) nadal działa
> `sudo apt install raspberrypi-kernel-headers`.

Sprawdź, że nagłówki są na miejscu:

```bash
ls /lib/modules/$(uname -r)/build
```

### 2. Pobranie i kompilacja

```bash
git clone https://github.com/uvoelkel/cberryfb.git
cd cberryfb
make
```

Kompilacja zajmuje kilka–kilkanaście sekund (sam moduł i overlay,
nie całe jądro).

### 3. Instalacja

```bash
sudo make install
```

Cel `install` kopiuje:

- `cberryfb.ko`  → `/lib/modules/$(uname -r)/extra/`
- `cberry.dtbo` → `/boot/firmware/overlays/` (lub `/boot/overlays/` na
  starszych systemach — wykrywane automatycznie)

i odświeża zależności (`depmod -a`).

### 4. Włączenie SPI i overlay'a

W `/boot/firmware/config.txt` (Bookworm) lub `/boot/config.txt` (starsze)
dodaj:

```
dtparam=spi=on
dtoverlay=cberry
```

Krócej:

```bash
BOOT=/boot/firmware; [ -d $BOOT ] || BOOT=/boot
echo "dtparam=spi=on"   | sudo tee -a $BOOT/config.txt
echo "dtoverlay=cberry" | sudo tee -a $BOOT/config.txt
```

### 5. Reboot

```bash
sudo reboot
```

Po starcie:

```bash
dmesg | grep -i cberry
ls /dev/fb*
```

Powinno pojawić się urządzenie `fb1` (lub kolejny dostępny indeks)
oraz wpis `admatec C-Berry LCD framebuffer device`.


## Aktualizacja / odinstalowanie

Aktualizacja po `git pull`:

```bash
make clean && make && sudo make install
sudo modprobe -r cberryfb && sudo modprobe cberryfb
```

Pełna deinstalacja:

```bash
sudo make uninstall
sudo reboot
```


## Użycie

Automatyczne ładowanie modułu po starcie:

```bash
echo cberryfb | sudo tee /etc/modules-load.d/cberryfb.conf
```

### X-server

```bash
FRAMEBUFFER=/dev/fb1 startx
```

### mplayer

```bash
mplayer -nolirc -vo fbdev2:/dev/fb1 -vf scale=320:-3 video.mpg
```

### Wyświetlenie obrazu (fbi)

```bash
sudo fbi -d /dev/fb1 -T 1 -noverbose -a image.bmp
```

### Mapowanie konsoli

```bash
con2fbmap 1 1   # konsola 1 -> fb1
con2fbmap 1 0   # przywróć fb0
```

### Podświetlenie

```bash
cat /sys/class/backlight/cberryfb/actual_brightness
cat /sys/class/backlight/cberryfb/max_brightness

echo 100 | sudo tee /sys/class/backlight/cberryfb/brightness
echo 0   | sudo tee /sys/class/backlight/cberryfb/brightness
echo 255 | sudo tee /sys/class/backlight/cberryfb/brightness
```


## Device Tree overlay

Repozytorium zawiera nakładkę [cberry.dts](cberry.dts), kompilowaną
automatycznie przez `make` do `cberry.dtbo`. Overlay:

- konfiguruje wpis pod `spi0.1` (CE1) i wyłącza domyślny `spidev1`,
- definiuje GPIO sterujące LCD (OE, RS, CS, WR, RESET, opcjonalnie WAIT),
- podaje `compatible = "admatec,cberry"`, dzięki czemu
  `cberryfb` wiąże się automatycznie po załadowaniu modułu.


## Diagnostyka

```bash
modinfo cberryfb              # m.in. vermagic
ls /dev/spidev*               # czy SPI działa
dtoverlay -l                  # wczytane overlay'e
dmesg | grep -Ei 'cberry|spi|fb'
```

Najczęstsze problemy:

| Objaw                                     | Prawdopodobna przyczyna                                |
|-------------------------------------------|--------------------------------------------------------|
| `modprobe: ERROR: could not insert ...`   | Niezgodne `vermagic` — `make clean && make` po update'cie jądra |
| Brak `/dev/fb1`                           | Brak `dtoverlay=cberry` w `config.txt` lub SPI wyłączony |
| Czarny ekran, ale `/dev/fb1` jest         | Sprawdź podświetlenie: `echo 255 \| sudo tee /sys/class/backlight/cberryfb/brightness` |


## Parametry modułu

```bash
modinfo -p cberryfb
```

- `fps` — częstotliwość odświeżania deferred-IO (domyślnie 20).

Ustawianie przy ładowaniu:

```bash
sudo modprobe cberryfb fps=30
```

lub na stałe w `/etc/modprobe.d/cberryfb.conf`:

```
options cberryfb fps=30
```


## Zaawansowane: cross-compile na komputerze deweloperskim

Jeśli chcesz budować moduł poza Pi (np. na macOS / x86 Linuksie),
w repo są pliki [Dockerfile.cross](Dockerfile.cross) oraz
[build-cross.sh](build-cross.sh). Skrypt łączy się przez SSH do Pi,
odczytuje `uname -r`/`uname -m`/model, dobiera odpowiednią gałąź jądra
i toolchain, buduje moduł w kontenerze i weryfikuje `vermagic`.

Większość użytkowników tego nie potrzebuje — natywne `make` na Pi
z `raspberrypi-kernel-headers` wystarczy.


## Zaawansowane: wbudowanie w drzewo jądra

Jeśli budujesz całe jądro od zera, dodaj sterownik do tree:

```bash
git clone https://github.com/uvoelkel/cberryfb.git \
    drivers/video/fbdev/cberryfb
```

W `drivers/video/fbdev/Makefile` dopisz:

```make
obj-\$(CONFIG_FB_CBERRY)   += cberryfb/
```

W `drivers/video/fbdev/Kconfig` (przed `endmenu`):

```kconfig
source "drivers/video/fbdev/cberryfb/Kconfig"
```

Potem `make menuconfig` → *Device Drivers* → *Graphics support*
→ *Frame buffer Devices* → `<M> C-Berry LCD frame buffer support`.


## Licencja

GPLv2 (zgodnie z modułem jądra Linux).
