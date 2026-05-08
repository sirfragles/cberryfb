/*
 * cberry_vendortest.c — verbatim copy of vendor tft_test.c main() with
 * the BMP source replaced by a solid-colour fill. Links against vendor
 * tft.c + RAIO8870.c, so the only difference vs the working vendor demo
 * is the source of pixel bytes.
 *
 * Build: tools/run_vendortest.sh
 * Run:   sudo ./cberry_vendortest [red|green|blue|white|black]
 */

#include <bcm2835.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "tft.h"
#include "RAIO8870.h"

int main(int argc, char **argv)
{
    uint16_t picture[1][PICTURE_PIXELS];
    int j;

    const char *col = (argc > 1) ? argv[1] : "red";
    uint16_t pix565;
    if      (!strcmp(col, "red"))    pix565 = 0xF800;
    else if (!strcmp(col, "green"))  pix565 = 0x07E0;
    else if (!strcmp(col, "blue"))   pix565 = 0x001F;
    else if (!strcmp(col, "white"))  pix565 = 0xFFFF;
    else if (!strcmp(col, "black"))  pix565 = 0x0000;
    else { fprintf(stderr, "unknown colour %s\n", col); return 1; }

    if (!bcm2835_init())
        return 1;

    TFT_init_board();
    TFT_hard_reset();
    RAIO_init();

    /* Vendor preamble — kept verbatim because this exact sequence
     * precedes the BMP upload in the vendor demo that is proven to
     * work on this hardware. */
    Draw_Square(0, 0, 319, 239);
    Text_Foreground_Color(COLOR_BLACK);
    RAIO_StartDrawing(SQUARE_FILL);

    Text_Foreground_Color(COLOR_BLUE);
    Draw_Square(210, 150, 260, 200);

    Draw_Line(10, 230, 310, 10);
    Text_Foreground_Color(COLOR_GREEN);
    RAIO_StartDrawing(LINE);

    Draw_Circle(90, 65, 25);
    Text_Foreground_Color(COLOR_RED);
    RAIO_StartDrawing(CIRCLE_FILL);

    delay(2000);

    for (j = 0; j < PICTURE_PIXELS; j++)
        picture[0][j] = pix565;

    fprintf(stderr, "==> RAIO_Write_Picture solid 0x%04x (%s) %d px\n",
            pix565, col, PICTURE_PIXELS);
    RAIO_Write_Picture(&picture[0][0], PICTURE_PIXELS);

    fprintf(stderr, "==> done; panel should now be solid %s\n", col);
    delay(5000);

    bcm2835_close();
    return 0;
}
/*
 * cberry_vendortest.c — byte-for-byte copy of vendor tft_test.c main(),
 * with the BMP read replaced by a solid-colour fill in place of picture[].
 *
 * If vendor demo works on this hardware (and it does), this MUST work too,
 * because the only difference is the source of pixel bytes.
 *
 * Build: handled by tools/run_vendortest.sh (links vendor tft.c + RAIO8870.c).
 * Run:   sudo ./cberry_vendortest [red|green|blue|white|black]
 */

#include <bcm2835.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "tft.h"
#include "RAIO8870.h"

int main(int argc, char **argv)
{
    /* SAME stack layout as vendor tft_test.c */
    uint16_t picture[1][ PICTURE_PIXELS ];
    int j;

    const char *col = (argc > 1) ? argv[1] : "red";
    uint16_t pix565;
    if      (!strcmp(col, "red"))    pix565 = 0xF800;
    else if (!strcmp(col, "green"))  pix565 = 0x07E0;
    else if (!strcmp(col, "blue"))   pix565 = 0x001F;
    else if (!strcmp(col, "white"))  pix565 = 0xFFFF;
    else if (!strcmp(col, "black"))  pix565 = 0x0000;
    else { fprintf(stderr, "unknown colour %s\n", col); return 1; }

    if (!bcm2835_init())
        return 1;

    TFT_init_board();
    TFT_hard_reset();
    RAIO_init();

    /* IDENTICAL drawing preamble to vendor tft_test.c — included verbatim
     * because the vendor demo did this BEFORE the bitmap upload, and that
     * is the exact sequence proven to work on this hardware. */
    Draw_Square(0, 0, 319, 239);
    Text_Foreground_Color(COLOR_BLACK);
    RAIO_StartDrawing(SQUARE_FILL);

    Text_Foreground_Color(COLOR_BLUE);
    Draw_Square(210, 150, 260, 200);

    Draw_Line(10, 230, 310, 10);
    Text_Foreground_Color(COLOR_GREEN);
    RAIO_StartDrawing(LINE);

    Draw_Circle(90, 65, 25);
    Text_Foreground_Color(COLOR_RED);
    RAIO_StartDrawing(CIRCLE_FILL);

    delay(2000);

    /* Same buffer + same RAIO_Write_Picture path as vendor's BMP demo,
     * but with a solid colour. */
    for (j = 0; j < PICTURE_PIXELS; j++)
        picture[0][j] = pix565;

    fprintf(stderr, "==> RAIO_Write_Picture solid 0x%04x (%s) %d px\n",
            pix565, col, PICTURE_PIXELS);
    RAIO_Write_Picture(&picture[0][0], PICTURE_PIXELS);

    fprintf(stderr, "==> done; panel should now be solid %s\n", col);
    delay(5000);

    bcm2835_close();
    return 0;
}
/*
 * cberry_vendortest.c — link against vendor tft.c + RAIO8870.c verbatim
 * and fill the entire screen by pushing a solid-colour pixel buffer via
 * RAIO_Write_Picture. This is the SAME path vendor demo uses to display
 * its BMP, so if vendor demo works on this hardware, this MUST work too.
 *
 * Build:    handled by tools/run_vendortest.sh.
 * Run:      sudo ./cberry_vendortest [red|green|blue|white|black]
 */

#include <bcm2835.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include "tft.h"
#include "RAIO8870.h"

int main(int argc, char **argv)
{
    const char *col = (argc > 1) ? argv[1] : "red";
    /* SYSR=0x0A in vendor RAIO_init selects 16bpp 65K-colour mode, so
     * the pixel burst path expects RGB565. */
    uint16_t pix565;

    if      (!strcmp(col, "red"))    pix565 = 0xF800;
    else if (!strcmp(col, "green"))  pix565 = 0x07E0;
    else if (!strcmp(col, "blue"))   pix565 = 0x001F;
    else if (!strcmp(col, "white"))  pix565 = 0xFFFF;
    else if (!strcmp(col, "black"))  pix565 = 0x0000;
    else { fprintf(stderr, "unknown colour %s\n", col); return 1; }

    if (!bcm2835_init()) {
        fprintf(stderr, "bcm2835_init failed (run as root)\n");
        return 1;
    }

    TFT_init_board();
    TFT_hard_reset();
    RAIO_init();

    const uint32_t count = (uint32_t)DISPLAY_WIDTH * DISPLAY_HEIGHT;
    uint16_t *buf = malloc(count * sizeof(uint16_t));
    if (!buf) { fprintf(stderr, "oom\n"); bcm2835_close(); return 1; }
    for (uint32_t i = 0; i < count; i++) buf[i] = pix565;

    fprintf(stderr, "==> RAIO_Write_Picture solid 0x%04x (%s) %u px\n",
            pix565, col, count);
    RAIO_Write_Picture(buf, count);

    fprintf(stderr, "==> done; panel should be solid %s for 5 s\n", col);
    sleep(5);

    free(buf);
    bcm2835_close();
    return 0;
}
