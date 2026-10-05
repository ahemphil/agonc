/* graphics.c - drawing on the Agon's screen through <agon/vdp.h>: a
 * screen mode, colours, lines, filled shapes and text in places.
 *
 *     agonc -o graphics.bin graphics.c
 *     graphics
 *
 * Every call here sends a few VDU bytes to the VDP, the Agon's display
 * processor, which does the drawing. Graphics positions are in the VDP's
 * logical units: the screen is 1280 wide and 1024 high whatever the mode,
 * with (0, 0) at the bottom left. Text positions are character cells,
 * counted from the top left. Press a key at the end to finish.
 */

#include <stdio.h>
#include <agon/vdp.h>
#include <agon/mos.h>

/* The first sixteen colours of a 16-colour mode. */
#define BLACK 0
#define RED 1
#define GREEN 2
#define YELLOW 3
#define BLUE 4
#define MAGENTA 5
#define CYAN 6
#define WHITE 7
#define GREY 8
#define BRIGHT_WHITE 15

/* A label in text, at column x of row y. */
static void label(int x, int y, const char *text)
{
    vdp_tab(x, y);
    printf("%s", text);
}

int main(void)
{
    int i;

    vdp_mode(0);                        /* 640 x 480 pixels, 16 colours */
    vdp_cursor(0);                      /* hide the text cursor while drawing */
    vdp_colour(BRIGHT_WHITE);
    label(0, 0, "agonc graphics example");

    /* An outline: move to a corner, then draw from point to point.
     * vdp_gcol's first argument is the paint mode; 0 just sets pixels. */
    vdp_gcol(0, WHITE);
    vdp_move(40, 40);
    vdp_draw(1240, 40);
    vdp_draw(1240, 900);
    vdp_draw(40, 900);
    vdp_draw(40, 40);

    /* A fan of lines from the bottom-left corner. */
    vdp_gcol(0, CYAN);
    for (i = 0; i <= 10; i++) {
        vdp_move(60, 60);
        vdp_draw(60 + i * 40, 460);
        vdp_move(60, 60);
        vdp_draw(460, 60 + i * 40);
    }

    /* Filled shapes take their corners from the last points visited:
     * a rectangle from the last point to the one given, a triangle with
     * the last two, a circle centred on the last point through the one
     * given. */
    vdp_gcol(0, RED);
    vdp_move(560, 120);
    vdp_filled_rect(760, 420);

    vdp_gcol(0, GREEN);
    vdp_move(820, 120);
    vdp_move(1020, 120);
    vdp_triangle(920, 420);

    vdp_gcol(0, YELLOW);
    vdp_move(1130, 270);
    vdp_circle(1210, 270);

    /* Text in colour, and on a coloured background (128 + colour). Mode 0
     * has 80 columns and 60 rows of 8 by 8 pixels, and a logical unit is
     * half a pixel across: row 54 is just below the shapes, each word under
     * its own, and row 58 below the outline. */
    vdp_colour(WHITE);
    label(37, 54, "rectangle");
    label(54, 54, "triangle");
    label(68, 54, "circle");
    vdp_colour(BLACK);
    vdp_colour(128 + YELLOW);
    label(2, 58, " Press a key ");
    vdp_colour(128 + BLACK);

    mos_getkey();                       /* wait for any key */

    vdp_colour(WHITE);
    vdp_cls();
    vdp_cursor(1);
    return 0;
}
