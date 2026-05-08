// SPDX-License-Identifier: GPL-2.0
/*
 * Frame buffer driver for the admatec C-Berry 320x240 LCD module.
 *
 * Original work (2014, BCM2835-only, /dev/mem style):
 *     Copyright (C) 2014 Ulrich Völkel
 *     based on the bcm2835 library by Mike McCauley and the
 *     C-Berry example code by admatec GmbH.
 *
 * Modern rewrite (2026):
 *     - registers as an spi_driver bound through device tree
 *     - uses GPIO descriptors (gpiod) instead of mapping /dev/mem
 *     - uses the modern fb deferred-IO sysmem helpers
 *     - portable across Raspberry Pi 1 through Pi 5
 *
 * The on-board glue logic latches each 16-bit SPI word into the parallel
 * RAIO8870 controller using a separate WR strobe. This driver therefore
 * issues one SPI transfer per RAIO word and toggles the WR/RS/CS GPIOs
 * around it.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/spi/spi.h>
#include <linux/vmalloc.h>

#define DRV_NAME		"cberryfb"

/* 320 x 240 16bpp RGB565 */
#define DISPLAY_WIDTH		320
#define DISPLAY_HEIGHT		240
#define DISPLAY_BPP		16
#define DISPLAY_BYTES		(DISPLAY_WIDTH * DISPLAY_HEIGHT * (DISPLAY_BPP / 8))

/* RAIO8870 register subset actually used during init / runtime. */
#define RAIO_PWRR		0x01
#define RAIO_MRWC		0x02
#define RAIO_PCLK		0x04
#define RAIO_SYSR		0x10
#define RAIO_IODR		0x13
#define RAIO_HDWR		0x14
#define RAIO_HNDFTR		0x15
#define RAIO_HNDR		0x16
#define RAIO_HSTR		0x17
#define RAIO_HPWR		0x18
#define RAIO_VDHR0		0x19
#define RAIO_VDHR1		0x1a
#define RAIO_VNDR0		0x1b
#define RAIO_VNDR1		0x1c
#define RAIO_VPWR		0x1f
#define RAIO_DPCR		0x20
#define RAIO_TBCR		0x43
#define RAIO_HSAW0		0x30
#define RAIO_HSAW1		0x31
#define RAIO_VSAW0		0x32
#define RAIO_VSAW1		0x33
#define RAIO_HEAW0		0x34
#define RAIO_HEAW1		0x35
#define RAIO_VEAW0		0x36
#define RAIO_VEAW1		0x37
#define RAIO_P1CR		0x8a
#define RAIO_P1DCR		0x8b
#define RAIO_PLLC1		0x88
#define RAIO_PLLC2		0x89
#define RAIO_MCLR		0x8e

#define RAIO_BL_PWM_MAX		0xff

#define CBERRY_DEFAULT_FPS	25

static unsigned int fps = CBERRY_DEFAULT_FPS;
module_param(fps, uint, 0444);
MODULE_PARM_DESC(fps, "Maximum frame rate of deferred IO updates");

static unsigned int brightness = 180;
module_param(brightness, uint, 0644);
MODULE_PARM_DESC(brightness,
	"Backlight value (0-255) applied automatically when the first\n"
	"\t\tframebuffer content is drawn. After that, control is handed\n"
	"\t\toff to userspace via /sys/class/backlight/cberryfb/brightness.\n"
	"\t\tSet to 0 to keep the panel dark until userspace turns it on.");

struct cberryfb {
	struct spi_device	*spi;
	struct fb_info		*info;

	/* GPIO descriptors (acquired with gpiod, polarity from DT). */
	struct gpio_desc	*oe;
	struct gpio_desc	*rs;
	struct gpio_desc	*cs;	/* RAIO chip select (separate from SPI CE) */
	struct gpio_desc	*wr;
	struct gpio_desc	*rd;	/* read strobe — kept deasserted */
	struct gpio_desc	*reset;
	struct gpio_desc	*wait;	/* input: 1 == ready */

	struct mutex		io_lock;	/* serialises bus access */

	u32			palette[16];
	u8			brightness;
	bool			auto_lit_done;	/* first frame already lit the panel */
};

/* ------------------------------------------------------------------ */
/* Low-level bus helpers                                              */
/* ------------------------------------------------------------------ */

/*
 * Write one 16-bit word (high byte first) to the controller. The caller
 * is responsible for setting RS, CS, WR, OE and serialising access.
 */
static int cberryfb_spi_word(struct cberryfb *cb, u16 word)
{
	u8 buf[2] = { word >> 8, word & 0xff };

	return spi_write(cb->spi, buf, sizeof(buf));
}

static int cberryfb_write_register(struct cberryfb *cb, u16 reg)
{
	int ret;

	gpiod_set_value_cansleep(cb->rs, 1);	/* RS = 1 -> register select */
	gpiod_set_value_cansleep(cb->cs, 1);
	gpiod_set_value_cansleep(cb->wr, 1);
	gpiod_set_value_cansleep(cb->oe, 1);

	ret = cberryfb_spi_word(cb, reg);

	gpiod_set_value_cansleep(cb->wr, 0);
	gpiod_set_value_cansleep(cb->cs, 0);
	gpiod_set_value_cansleep(cb->oe, 0);

	return ret;
}

static int cberryfb_write_data(struct cberryfb *cb, u16 data)
{
	int ret;

	gpiod_set_value_cansleep(cb->rs, 0);	/* RS = 0 -> data */
	gpiod_set_value_cansleep(cb->cs, 1);
	gpiod_set_value_cansleep(cb->wr, 1);
	gpiod_set_value_cansleep(cb->oe, 1);

	ret = cberryfb_spi_word(cb, data);

	gpiod_set_value_cansleep(cb->wr, 0);
	gpiod_set_value_cansleep(cb->cs, 0);
	gpiod_set_value_cansleep(cb->oe, 0);

	return ret;
}

static int cberryfb_set_register(struct cberryfb *cb, u8 reg, u8 value)
{
	int ret;

	ret = cberryfb_write_register(cb, reg);
	if (ret)
		return ret;
	return cberryfb_write_data(cb, value);
}

static void cberryfb_wait_busy(struct cberryfb *cb)
{
	int val;
	int ret;

	if (!cb->wait)
		return;
	/* The WAIT pin goes high when the controller is idle. */
	ret = readx_poll_timeout(gpiod_get_value_cansleep, cb->wait,
				 val, val == 1,
				 50, 500 * USEC_PER_MSEC);
	if (ret)
		dev_warn(&cb->spi->dev, "RAIO busy wait timed out\n");
}

/* ------------------------------------------------------------------ */
/* Pixel data path                                                    */
/* ------------------------------------------------------------------ */

/*
 * Push the entire shadow buffer to the controller. The framebuffer
 * is laid out as native-endian RGB565 words (DISPLAY_WIDTH*DISPLAY_HEIGHT
 * of them); RAIO expects them MSB first which is exactly what
 * cberryfb_spi_word does.
 *
 * Each word needs its own WR strobe, so we cannot bundle the whole
 * frame into a single spi_write. We do, however, hold RAIO_CS asserted
 * across the entire burst.
 */
static void cberryfb_update_display(struct fb_info *info)
{
	struct cberryfb *cb = info->par;
	const u16 *src = (const u16 *)info->screen_buffer;
	size_t pixels = (size_t)DISPLAY_WIDTH * DISPLAY_HEIGHT;
	size_t i;

	mutex_lock(&cb->io_lock);

	if (cberryfb_write_register(cb, RAIO_MRWC))
		goto out;

	/* Set up signal levels for a long data burst. */
	gpiod_set_value_cansleep(cb->rs, 0);
	gpiod_set_value_cansleep(cb->cs, 1);
	gpiod_set_value_cansleep(cb->oe, 1);

	for (i = 0; i < pixels; i++) {
		gpiod_set_value_cansleep(cb->wr, 1);
		if (cberryfb_spi_word(cb, src[i]))
			break;
		gpiod_set_value_cansleep(cb->wr, 0);
	}

	gpiod_set_value_cansleep(cb->cs, 0);
	gpiod_set_value_cansleep(cb->oe, 0);

out:
	mutex_unlock(&cb->io_lock);

	/* First time userspace draws something into the framebuffer, light
	 * the panel up to the configured default. After that, the backlight
	 * is owned by sysfs (/sys/class/backlight/cberryfb/brightness) and
	 * we never touch it from the data path again. */
	if (!cb->auto_lit_done) {
		u8 want = min_t(unsigned int, brightness, RAIO_BL_PWM_MAX);

		cb->auto_lit_done = true;
		if (want && info->bl_dev) {
			info->bl_dev->props.brightness = want;
			backlight_update_status(info->bl_dev);
		}
	}
}

/* ------------------------------------------------------------------ */
/* fbdev callbacks                                                    */
/* ------------------------------------------------------------------ */

static void cberryfb_defio_callback(struct fb_info *info,
				    struct list_head *pagereflist)
{
	cberryfb_update_display(info);
}

static void cberryfb_defio_damage_range(struct fb_info *info, off_t off,
					size_t len)
{
	cberryfb_update_display(info);
}

static void cberryfb_defio_damage_area(struct fb_info *info, u32 x, u32 y,
				       u32 width, u32 height)
{
	cberryfb_update_display(info);
}

FB_GEN_DEFAULT_DEFERRED_SYSMEM_OPS(cberryfb,
				   cberryfb_defio_damage_range,
				   cberryfb_defio_damage_area);

static int cberryfb_setcolreg(unsigned int regno, unsigned int red,
			      unsigned int green, unsigned int blue,
			      unsigned int transp, struct fb_info *info)
{
	u32 v;

	if (info->fix.visual != FB_VISUAL_TRUECOLOR)
		return 0;
	if (regno >= 16)
		return -EINVAL;

	v = (red   << info->var.red.offset)   |
	    (green << info->var.green.offset) |
	    (blue  << info->var.blue.offset);
	((u32 *)info->pseudo_palette)[regno] = v;

	return 0;
}

static int cberryfb_blank(int blank_mode, struct fb_info *info)
{
	switch (blank_mode) {
	case FB_BLANK_UNBLANK:
	case FB_BLANK_NORMAL:
		memset(info->screen_buffer, 0, info->fix.smem_len);
		cberryfb_update_display(info);
		return 0;
	case FB_BLANK_POWERDOWN:
	case FB_BLANK_HSYNC_SUSPEND:
	case FB_BLANK_VSYNC_SUSPEND:
		return 0;
	default:
		return -EINVAL;
	}
}

static const struct fb_ops cberryfb_ops = {
	.owner		= THIS_MODULE,
	FB_DEFAULT_DEFERRED_OPS(cberryfb),
	.fb_setcolreg	= cberryfb_setcolreg,
	.fb_blank	= cberryfb_blank,
};

static const struct fb_fix_screeninfo cberryfb_fix = {
	.id		= DRV_NAME,
	.type		= FB_TYPE_PACKED_PIXELS,
	.visual		= FB_VISUAL_TRUECOLOR,
	.accel		= FB_ACCEL_NONE,
	.line_length	= DISPLAY_WIDTH * DISPLAY_BPP / 8,
};

static const struct fb_var_screeninfo cberryfb_var = {
	.width		= DISPLAY_WIDTH,
	.height		= DISPLAY_HEIGHT,
	.bits_per_pixel	= DISPLAY_BPP,
	.xres		= DISPLAY_WIDTH,
	.yres		= DISPLAY_HEIGHT,
	.xres_virtual	= DISPLAY_WIDTH,
	.yres_virtual	= DISPLAY_HEIGHT,
	.activate	= FB_ACTIVATE_NOW,
	.vmode		= FB_VMODE_NONINTERLACED,
	.red		= { .offset = 11, .length = 5 },
	.green		= { .offset =  5, .length = 6 },
	.blue		= { .offset =  0, .length = 5 },
};

/* ------------------------------------------------------------------ */
/* Backlight                                                          */
/* ------------------------------------------------------------------ */

static int cberryfb_set_backlight(struct cberryfb *cb, u8 value)
{
	int ret;

	mutex_lock(&cb->io_lock);
	if (value == 0) {
		/* Disable PWM1 entirely so the LED driver gets a clean LOW
		 * (no residual duty, no carrier). Avoids any sub-percent
		 * glow that the LED can pick up from a 0%-duty PWM. */
		ret = cberryfb_set_register(cb, RAIO_P1DCR, 0x00);
		if (!ret)
			ret = cberryfb_set_register(cb, RAIO_P1CR, 0x00);
	} else {
		/* Write the duty before enabling, so the chip does not
		 * latch a stale P1DCR for one PWM cycle when PWM1 turns on. */
		ret = cberryfb_set_register(cb, RAIO_P1DCR, value);
		if (!ret)
			ret = cberryfb_set_register(cb, RAIO_P1CR, 0x88);
	}
	if (!ret)
		cb->brightness = value;
	mutex_unlock(&cb->io_lock);

	return ret;
}

static int cberryfb_bl_update_status(struct backlight_device *bdev)
{
	struct cberryfb *cb = bl_get_data(bdev);
	u8 value = bdev->props.brightness;

	if (bdev->props.power != FB_BLANK_UNBLANK ||
	    bdev->props.state & (BL_CORE_SUSPENDED | BL_CORE_FBBLANK))
		value = 0;

	if (value == cb->brightness)
		return 0;

	return cberryfb_set_backlight(cb, value);
}

static int cberryfb_bl_get_brightness(struct backlight_device *bdev)
{
	struct cberryfb *cb = bl_get_data(bdev);

	return cb->brightness;
}

static const struct backlight_ops cberryfb_bl_ops = {
	.update_status	= cberryfb_bl_update_status,
	.get_brightness	= cberryfb_bl_get_brightness,
};

/* ------------------------------------------------------------------ */
/* Hardware bring-up                                                  */
/* ------------------------------------------------------------------ */

static void cberryfb_hard_reset(struct cberryfb *cb)
{
	gpiod_set_value_cansleep(cb->reset, 1);	/* assert reset (active low in DT) */
	msleep(10);
	gpiod_set_value_cansleep(cb->reset, 0);
	msleep(2);
}

static int cberryfb_raio_init(struct cberryfb *cb)
{
	int ret;

	mutex_lock(&cb->io_lock);

	ret = cberryfb_set_register(cb, RAIO_PLLC1, 0x07);
	if (ret)
		goto out;
	udelay(200);
	ret = cberryfb_set_register(cb, RAIO_PLLC2, 0x03);
	if (ret)
		goto out;
	udelay(200);

	cberryfb_set_register(cb, RAIO_PWRR, 0x01);
	cberryfb_set_register(cb, RAIO_PWRR, 0x00);
	msleep(100);

	cberryfb_set_register(cb, RAIO_SYSR, 0x0a);
	cberryfb_set_register(cb, RAIO_DPCR, 0x00);

	cberryfb_set_register(cb, RAIO_HDWR,   (DISPLAY_WIDTH / 8) - 1);
	cberryfb_set_register(cb, RAIO_HNDFTR, 0x02);
	cberryfb_set_register(cb, RAIO_HNDR,   0x03);
	cberryfb_set_register(cb, RAIO_HSTR,   0x04);
	cberryfb_set_register(cb, RAIO_HPWR,   0x03);

	cberryfb_set_register(cb, RAIO_VDHR0, (DISPLAY_HEIGHT - 1) & 0xff);
	cberryfb_set_register(cb, RAIO_VDHR1, (DISPLAY_HEIGHT - 1) >> 8);
	cberryfb_set_register(cb, RAIO_VNDR0, 0x10);
	cberryfb_set_register(cb, RAIO_VNDR1, 0x00);
	cberryfb_set_register(cb, RAIO_VPWR,  0x00);

	/* Active window covers the full panel. */
	cberryfb_set_register(cb, RAIO_HSAW0, 0);
	cberryfb_set_register(cb, RAIO_HSAW1, 0);
	cberryfb_set_register(cb, RAIO_HEAW0, (DISPLAY_WIDTH - 1) & 0xff);
	cberryfb_set_register(cb, RAIO_HEAW1, (DISPLAY_WIDTH - 1) >> 8);
	cberryfb_set_register(cb, RAIO_VSAW0, 0);
	cberryfb_set_register(cb, RAIO_VSAW1, 0);
	cberryfb_set_register(cb, RAIO_VEAW0, (DISPLAY_HEIGHT - 1) & 0xff);
	cberryfb_set_register(cb, RAIO_VEAW1, (DISPLAY_HEIGHT - 1) >> 8);

	cberryfb_set_register(cb, RAIO_PCLK, 0x00);

	/* Force the backlight PWM into a known-OFF state. RAIO8870's reset
	 * default for P1CR/P1DCR is documented as 0x00 (PWM disabled, output
	 * low) but some HAT revisions still latch the LED driver enable
	 * high after hard_reset, lighting the panel as soon as raio_init
	 * enables PWRR/IODR below. Writing duty=0 first and then disabling
	 * PWM unconditionally guarantees the LED is dark when we hand off
	 * to userspace. cberryfb_set_backlight() turns it back on later. */
	cberryfb_set_register(cb, RAIO_P1DCR, 0x00);
	cberryfb_set_register(cb, RAIO_P1CR,  0x00);
	cb->brightness = 0;

	/* Clear the on-controller memory with the background colour. */
	cberryfb_set_register(cb, RAIO_TBCR, 0x00);
	cberryfb_set_register(cb, RAIO_MCLR, 0x81);
	cberryfb_wait_busy(cb);

	cberryfb_set_register(cb, RAIO_IODR, 0x07);
	cberryfb_set_register(cb, RAIO_PWRR, 0x80);

out:
	mutex_unlock(&cb->io_lock);
	return ret;
}

/* ------------------------------------------------------------------ */
/* Probe / remove                                                     */
/* ------------------------------------------------------------------ */

static int cberryfb_request_gpios(struct cberryfb *cb)
{
	struct device *dev = &cb->spi->dev;

	cb->oe    = devm_gpiod_get(dev, "oe",    GPIOD_OUT_LOW);
	if (IS_ERR(cb->oe))
		return dev_err_probe(dev, PTR_ERR(cb->oe), "oe-gpios\n");

	cb->rs    = devm_gpiod_get(dev, "rs",    GPIOD_OUT_LOW);
	if (IS_ERR(cb->rs))
		return dev_err_probe(dev, PTR_ERR(cb->rs), "rs-gpios\n");

	cb->cs    = devm_gpiod_get(dev, "lcd-cs", GPIOD_OUT_LOW);
	if (IS_ERR(cb->cs))
		return dev_err_probe(dev, PTR_ERR(cb->cs), "lcd-cs-gpios\n");

	cb->wr    = devm_gpiod_get(dev, "wr",    GPIOD_OUT_LOW);
	if (IS_ERR(cb->wr))
		return dev_err_probe(dev, PTR_ERR(cb->wr), "wr-gpios\n");

	/* RD must be parked deasserted (HIGH on the bus) — without this,
	 * RAIO can interpret bus traffic as a continuous read cycle and
	 * silently drop every command we issue. The vendor SDK explicitly
	 * sets RD=HIGH at boot. We use _optional so older overlays without
	 * rd-gpios still load. */
	cb->rd    = devm_gpiod_get_optional(dev, "rd", GPIOD_OUT_LOW);
	if (IS_ERR(cb->rd))
		return dev_err_probe(dev, PTR_ERR(cb->rd), "rd-gpios\n");

	/* Park the chip in reset right when we claim the GPIO. The C-Berry HAT
	 * shares the Pi's 5 V rail and has no power switch, so the RAIO retains
	 * its register state (incl. PWM duty) across `poweroff`/reboot. Holding
	 * the controller in reset until cberryfb_hard_reset() runs guarantees
	 * a clean slate — no stale backlight, no stale framebuffer contents. */
	cb->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(cb->reset))
		return dev_err_probe(dev, PTR_ERR(cb->reset), "reset-gpios\n");

	cb->wait  = devm_gpiod_get_optional(dev, "wait", GPIOD_IN);
	if (IS_ERR(cb->wait))
		return dev_err_probe(dev, PTR_ERR(cb->wait), "wait-gpios\n");

	return 0;
}

static int cberryfb_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct backlight_properties bl_props = {
		.type		= BACKLIGHT_RAW,
		.max_brightness	= RAIO_BL_PWM_MAX,
		.brightness	= 0,
	};
	struct backlight_device *bldev;
	struct fb_deferred_io *defio;
	struct cberryfb *cb;
	struct fb_info *info;
	void *vmem;
	int ret;

	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	ret = spi_setup(spi);
	if (ret)
		return dev_err_probe(dev, ret, "spi_setup\n");

	info = framebuffer_alloc(sizeof(*cb), dev);
	if (!info)
		return -ENOMEM;

	cb = info->par;
	cb->spi  = spi;
	cb->info = info;
	mutex_init(&cb->io_lock);

	ret = cberryfb_request_gpios(cb);
	if (ret)
		goto err_release;

	vmem = vzalloc(DISPLAY_BYTES);
	if (!vmem) {
		ret = -ENOMEM;
		goto err_release;
	}

	defio = devm_kzalloc(dev, sizeof(*defio), GFP_KERNEL);
	if (!defio) {
		ret = -ENOMEM;
		goto err_vfree;
	}
	defio->delay		= HZ / clamp(fps, 1u, 60u);
	defio->deferred_io	= cberryfb_defio_callback;

	info->fbops		= &cberryfb_ops;
	info->fix		= cberryfb_fix;
	info->fix.smem_start	= (unsigned long)vmem;
	info->fix.smem_len	= DISPLAY_BYTES;
	info->var		= cberryfb_var;
	info->screen_buffer	= vmem;
	info->flags		= FBINFO_VIRTFB;
	info->pseudo_palette	= cb->palette;
	info->fbdefio		= defio;
	fb_deferred_io_init(info);

	spi_set_drvdata(spi, cb);

	cberryfb_hard_reset(cb);
	ret = cberryfb_raio_init(cb);
	if (ret) {
		dev_err(dev, "RAIO init failed: %d\n", ret);
		goto err_defio;
	}

	bldev = devm_backlight_device_register(dev, DRV_NAME, dev, cb,
					       &cberryfb_bl_ops, &bl_props);
	if (IS_ERR(bldev)) {
		ret = PTR_ERR(bldev);
		goto err_defio;
	}
	info->bl_dev = bldev;

	ret = register_framebuffer(info);
	if (ret < 0) {
		dev_err(dev, "register_framebuffer: %d\n", ret);
		goto err_defio;
	}

	dev_info(dev, "fb%d: admatec C-Berry LCD framebuffer device\n",
		 info->node);
	return 0;

err_defio:
	fb_deferred_io_cleanup(info);
err_vfree:
	vfree(vmem);
err_release:
	framebuffer_release(info);
	return ret;
}

/* Put the RAIO controller into a state that survives reboot/poweroff
 * with the display off. The C-Berry HAT shares the Pi's 5 V rail and
 * has no power switch, so register contents persist across software
 * resets/reboots until the USB cable is unplugged. The next boot would
 * therefore see the panel lit at whatever PWM and display state we left
 * behind — unless we explicitly disable display + PWM here. */
static void cberryfb_park_chip(struct cberryfb *cb)
{
	cberryfb_set_backlight(cb, 0);

	mutex_lock(&cb->io_lock);
	/* PWRR bit 7 = display enable. Clear it so the panel pixels go
	 * black even if the framebuffer/PWM regs survive the reboot. */
	cberryfb_set_register(cb, RAIO_PWRR, 0x00);
	/* IODR controls the panel I/O drivers; zeroing it stops the chip
	 * driving the LCD glass at all. */
	cberryfb_set_register(cb, RAIO_IODR, 0x00);
	mutex_unlock(&cb->io_lock);

	/* Finally re-assert RST. On the next boot, even if 5 V never
	 * dropped, the chip will be in a defined off state until our
	 * driver probes and runs cberryfb_raio_init() again. */
	gpiod_set_value_cansleep(cb->reset, 1);
}

static void cberryfb_remove(struct spi_device *spi)
{
	struct cberryfb *cb = spi_get_drvdata(spi);
	struct fb_info *info = cb->info;
	void *vmem = info->screen_buffer;

	cberryfb_park_chip(cb);

	unregister_framebuffer(info);
	fb_deferred_io_cleanup(info);
	framebuffer_release(info);
	vfree(vmem);
}

static void cberryfb_shutdown(struct spi_device *spi)
{
	struct cberryfb *cb = spi_get_drvdata(spi);

	if (!cb)
		return;

	dev_info(&spi->dev, "shutdown: parking RAIO chip (display+PWM off)\n");
	cberryfb_park_chip(cb);
}

static const struct of_device_id cberryfb_of_match[] = {
	{ .compatible = "admatec,cberry" },
	{ }
};
MODULE_DEVICE_TABLE(of, cberryfb_of_match);

static const struct spi_device_id cberryfb_spi_ids[] = {
	{ "cberry", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, cberryfb_spi_ids);

static struct spi_driver cberryfb_driver = {
	.driver = {
		.name		= DRV_NAME,
		.of_match_table	= cberryfb_of_match,
	},
	.id_table	= cberryfb_spi_ids,
	.probe		= cberryfb_probe,
	.remove		= cberryfb_remove,
	.shutdown	= cberryfb_shutdown,
};
module_spi_driver(cberryfb_driver);

MODULE_DESCRIPTION("admatec C-Berry LCD framebuffer driver");
MODULE_AUTHOR("Ulrich Völkel");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("spi:cberry");
