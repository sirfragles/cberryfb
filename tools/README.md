# cberry_diag.py — narzędzie diagnostyczne C-Berry

Standalone test sprzętu z poziomu Pythona, omijający sterownik jądra.
Pozwala wykluczyć kolejno: zasilanie HAT-a, dostęp do SPI, odpowiedź
RAIO8870, działanie podświetlenia i wyświetlanie pikseli.

## Przygotowanie Pi

1. Tymczasowo wyłącz nasz overlay, żeby zwolnić `/dev/spidev0.1` i wszystkie GPIO:

   ```bash
   sudo sed -i 's/^dtoverlay=cberry/#dtoverlay=cberry/' /boot/firmware/config.txt
   grep -E 'dtparam=spi|dtoverlay' /boot/firmware/config.txt
   ```

   Upewnij się, że `dtparam=spi=on` jest aktywne.

2. Reboot:

   ```bash
   sudo reboot
   ```

3. Po reboocie sprawdź:

   ```bash
   ls /dev/spidev*           # powinno pokazać 0.0 i 0.1
   lsmod | grep cberry       # powinno być puste
   sudo modprobe -r cberryfb # awaryjnie
   sudo apt install -y python3-spidev python3-rpi.gpio
   ```

## Uruchomienie

Pełen pipeline:

```bash
sudo ./tools/cberry_diag.py
```

Pojedyncze fazy:

```bash
sudo ./tools/cberry_diag.py --step env        # vcgencmd, lsmod, model
sudo ./tools/cberry_diag.py --step spidev     # czy /dev/spidev0.1 jest
sudo ./tools/cberry_diag.py --step gpios      # poziomy idle wszystkich pinów
sudo ./tools/cberry_diag.py --step reset      # hard reset + WAIT
sudo ./tools/cberry_diag.py --step backlight  # rampa P1DCR
sudo ./tools/cberry_diag.py --step init       # pełny init RAIO
sudo ./tools/cberry_diag.py --step pattern --pattern bars
sudo ./tools/cberry_diag.py --step pattern --pattern cycle
```

Inny zegar SPI (np. 4 MHz dla starszych shifterów):

```bash
sudo ./tools/cberry_diag.py --speed 4000000
```

## Jak czytać wyniki

| Faza        | Co weryfikuje                                              |
|-------------|------------------------------------------------------------|
| env         | Pi nie throttluje, sterownik nie wisi                     |
| spidev      | Magistrala SPI dostępna z user-space                       |
| gpios       | Idle: OE/CS/WR/RS/RST = HIGH, WAIT z pull-up = HIGH        |
| reset       | Po impulsie RST WAIT idzie HIGH → RAIO żyje                |
| backlight   | Wzrokowa zmiana jasności panelu                           |
| init        | Pełna sekwencja init RAIO bez timeoutu                    |
| pattern     | Test pikseli (kolory, paski)                              |

### Mapa decyzyjna

- **`reset` FAIL** → RAIO nie odpowiada → zasilanie / taśma / shifter
- **`backlight` brak reakcji wzrokowej** → komendy nie docierają → SPI / shifter
- **`backlight` działa, `pattern` czarny** → strobe WR / RS źle → zwolnić zegar, sprawdzić polaryzacje
- **wszystkie OK** → problem był po stronie sterownika jądra; porównaj `cberryfb.c`

## Powrót do sterownika

Po diagnostyce przywróć overlay:

```bash
sudo sed -i 's/^#dtoverlay=cberry/dtoverlay=cberry/' /boot/firmware/config.txt
sudo reboot
```
