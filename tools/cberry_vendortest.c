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

    /* Vendor preamble verbatim. */
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
