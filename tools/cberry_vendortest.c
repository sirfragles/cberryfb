/*
 * cberry_vendortest.c — link against vendor tft.c + RAIO8870.c verbatim
 * and fill the entire screen with one solid colour using vendor's own
 * Draw_Square + SQUARE_FILL primitive.
 *
 * If THIS works and our cberry_mintest.c does not, the bug is in our
 * mintest. If THIS does NOT work, the system state is wrong (overlay
 * still active, kernel module loaded, spidev grabbed by something else,
 * etc.) and no software fix will help until the environment is clean.
 *
 * Build:    handled by tools/run_vendortest.sh (links vendor sources).
 * Run:      sudo ./cberry_vendortest [red|green|blue|white|black]
 */

#include <bcm2835.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "tft.h"
#include "RAIO8870.h"

int main(int argc, char **argv)
{
    const char *col = (argc > 1) ? argv[1] : "red";
    uint8_t fg = COLOR_RED;

    if      (!strcmp(col, "red"))    fg = COLOR_RED;
    else if (!strcmp(col, "green"))  fg = COLOR_GREEN;
    else if (!strcmp(col, "blue"))   fg = COLOR_BLUE;
    else if (!strcmp(col, "white"))  fg = COLOR_WHITE;
    else if (!strcmp(col, "black"))  fg = COLOR_BLACK;
    else { fprintf(stderr, "unknown colour %s\n", col); return 1; }

    if (!bcm2835_init()) {
        fprintf(stderr, "bcm2835_init failed (run as root)\n");
        return 1;
    }

    TFT_init_board();
    TFT_hard_reset();
    RAIO_init();

    /* full-screen solid fill via vendor primitive */
    Text_Foreground_Color(fg);
    Draw_Square(0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);
    RAIO_StartDrawing(SQUARE_FILL);

    fprintf(stderr, "vendor init complete; panel should be solid %s\n", col);
    sleep(5);

    bcm2835_close();
    return 0;
}
