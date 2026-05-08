/*
 * cberry_mintest.c — minimal RAIO bring-up using libbcm2835.
 *
 * Replicates our driver's init sequence + a solid colour fill, but
 * over the same low-level register-poking path that the vendor demo
 * uses successfully. This isolates whether the bug is in the init
 * sequence (which we'd see fail here) or in the Python/spidev/gpio
 * abstraction layer (in which case this test passes).
 *
 * Build:
 *   gcc -O2 -o cberry_mintest cberry_mintest.c -lbcm2835 -lrt
 * Run (as root, with overlay disabled and module unloaded):
 *   sudo ./cberry_mintest [red|green|blue|white]
 */

#include <bcm2835.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* GPIO pins (BCM numbering) */
#define OE   17
#define RS   18
#define CS    8
#define WR   24
#define RD   23
#define RST  25
#define WAIT 22

/* RAIO registers (matched against vendor RAIO8870.h) */
#define PWRR  0x01
#define MRWC  0x02
#define PCLK  0x04
#define SYSR  0x10
#define IODR  0x13
#define HDWR  0x14
#define HNDFTR 0x15
#define HNDR  0x16
#define HSTR  0x17
#define HPWR  0x18
#define VDHR0 0x19
#define VDHR1 0x1A
#define VNDR0 0x1B
#define VNDR1 0x1C
#define VPWR  0x1F
#define DPCR  0x20
#define HSAW0 0x30
#define HSAW1 0x31
#define VSAW0 0x32
#define VSAW1 0x33
#define HEAW0 0x34
#define HEAW1 0x35
#define VEAW0 0x36
#define VEAW1 0x37
#define TBCR  0x43
#define BGCR0 0x60
#define BGCR1 0x61
#define BGCR2 0x62
#define PLLC1 0x88
#define PLLC2 0x89
#define P1CR  0x8A
#define P1DCR 0x8B
#define MCLR  0x8E

#define W 320
#define H 240

static void xfer16(uint16_t v)
{
    char buf[2] = { (char)(v >> 8), (char)(v & 0xFF) };
    bcm2835_spi_writenb(buf, 2);
}

static void reg_w(uint8_t reg)
{
    bcm2835_gpio_write(RS, HIGH);
    bcm2835_gpio_write(CS, LOW);
    bcm2835_gpio_write(WR, LOW);
    bcm2835_gpio_write(OE, LOW);
    xfer16(reg);
    bcm2835_gpio_write(WR, HIGH);
    bcm2835_gpio_write(CS, HIGH);
    bcm2835_gpio_write(OE, HIGH);
}

static void dat_w(uint16_t v)
{
    bcm2835_gpio_write(RS, LOW);
    bcm2835_gpio_write(CS, LOW);
    bcm2835_gpio_write(WR, LOW);
    bcm2835_gpio_write(OE, LOW);
    xfer16(v);
    bcm2835_gpio_write(WR, HIGH);
    bcm2835_gpio_write(CS, HIGH);
    bcm2835_gpio_write(OE, HIGH);
}

static void set_reg(uint8_t reg, uint8_t val)
{
    reg_w(reg);
    dat_w(val);
}

static void wait_busy(void)
{
    int i;
    for (i = 0; i < 1000; i++) {
        if (bcm2835_gpio_lev(WAIT))
            return;
        bcm2835_delay(1);
    }
}

static void hard_reset(void)
{
    bcm2835_gpio_write(RST, LOW);
    bcm2835_delay(50);
    bcm2835_gpio_write(RST, HIGH);
    bcm2835_delay(50);
}

static void init_board(void)
{
    bcm2835_gpio_fsel(OE,   BCM2835_GPIO_FSEL_OUTP); bcm2835_gpio_write(OE,   HIGH);
    bcm2835_gpio_fsel(RST,  BCM2835_GPIO_FSEL_OUTP); bcm2835_gpio_write(RST,  HIGH);
    bcm2835_gpio_fsel(CS,   BCM2835_GPIO_FSEL_OUTP); bcm2835_gpio_write(CS,   HIGH);
    bcm2835_gpio_fsel(RS,   BCM2835_GPIO_FSEL_OUTP); bcm2835_gpio_write(RS,   HIGH);
    bcm2835_gpio_fsel(WR,   BCM2835_GPIO_FSEL_OUTP); bcm2835_gpio_write(WR,   HIGH);
    bcm2835_gpio_fsel(RD,   BCM2835_GPIO_FSEL_OUTP); bcm2835_gpio_write(RD,   HIGH);
    bcm2835_gpio_fsel(WAIT, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_set_pud(WAIT, BCM2835_GPIO_PUD_UP);

    bcm2835_spi_setBitOrder(BCM2835_SPI_BIT_ORDER_MSBFIRST);
    bcm2835_spi_setDataMode(BCM2835_SPI_MODE0);
    bcm2835_spi_setClockDivider(BCM2835_SPI_CLOCK_DIVIDER_64);  /* ~4 MHz */
    bcm2835_spi_chipSelect(BCM2835_SPI_CS1);
    bcm2835_spi_setChipSelectPolarity(BCM2835_SPI_CS1, LOW);
}

static void raio_init(uint8_t r5, uint8_t g6, uint8_t b5)
{
    /* PLL */
    set_reg(PLLC1, 0x07); bcm2835_delayMicroseconds(200);
    set_reg(PLLC2, 0x03); bcm2835_delayMicroseconds(200);

    /* soft-reset */
    set_reg(PWRR, 0x01);
    set_reg(PWRR, 0x00);
    bcm2835_delay(100);

    set_reg(SYSR, 0x0A);
    set_reg(DPCR, 0x00);

    set_reg(HDWR,   (W / 8) - 1);
    set_reg(HNDFTR, 0x02);
    set_reg(HNDR,   0x03);
    set_reg(HSTR,   0x04);
    set_reg(HPWR,   0x03);

    set_reg(VDHR0, (H - 1) & 0xFF);
    set_reg(VDHR1, (H - 1) >> 8);
    set_reg(VNDR0, 0x10);
    set_reg(VNDR1, 0x00);
    set_reg(VPWR,  0x00);

    set_reg(HSAW0, 0); set_reg(HSAW1, 0);
    set_reg(HEAW0, (W - 1) & 0xFF); set_reg(HEAW1, (W - 1) >> 8);
    set_reg(VSAW0, 0); set_reg(VSAW1, 0);
    set_reg(VEAW0, (H - 1) & 0xFF); set_reg(VEAW1, (H - 1) >> 8);

    set_reg(PCLK, 0x00);

    set_reg(P1CR,  0x88);
    set_reg(P1DCR, 0xFF);

    set_reg(BGCR0, r5);
    set_reg(BGCR1, g6);
    set_reg(BGCR2, b5);
    set_reg(TBCR, 0xFF);

    set_reg(MCLR, 0x81);
    wait_busy();

    set_reg(IODR, 0x07);
    set_reg(PWRR, 0x80);
}

int main(int argc, char **argv)
{
    const char *col = (argc > 1) ? argv[1] : "red";
    uint8_t r = 0, g = 0, b = 0;

    if (!strcmp(col, "red"))         { r = 255; }
    else if (!strcmp(col, "green"))  { g = 255; }
    else if (!strcmp(col, "blue"))   { b = 255; }
    else if (!strcmp(col, "white"))  { r = g = b = 255; }
    else if (!strcmp(col, "black"))  { /* zero */ }
    else { fprintf(stderr, "unknown colour %s\n", col); return 1; }

    if (!bcm2835_init()) {
        fprintf(stderr, "bcm2835_init failed (run as root)\n");
        return 1;
    }
    if (!bcm2835_spi_begin()) {
        fprintf(stderr, "bcm2835_spi_begin failed\n");
        bcm2835_close();
        return 1;
    }

    init_board();
    hard_reset();
    raio_init(r >> 3, g >> 2, b >> 3);

    fprintf(stderr, "init complete; panel should be solid %s\n", col);
    sleep(3);

    bcm2835_spi_end();
    bcm2835_close();
    return 0;
}
