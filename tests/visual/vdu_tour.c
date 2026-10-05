/* vdu_tour.c - a look at <agon/vdp.h> on a real VDP: numbered screens,
 * each saying at the top what should be on it. Run it on the GUI emulator
 * or an Agon, look, and note the number of any screen that does not match
 * its description. N moves on (other keys are ignored); Escape stops.
 *
 * The byte-level tests (tests/libc_c/t_vdu.c, t_vdu2.c) prove each
 * function sends the documented bytes; this proves the VDP does what the
 * documentation says with them. Screens that need a newer VDP say which.
 * Not part of make check: it needs eyes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <agon/vdp.h>
#include <agon/mos.h>

#define BLACK 0
#define RED 1
#define GREEN 2
#define YELLOW 3
#define BLUE 4
#define MAGENTA 5
#define CYAN 6
#define WHITE 7
#define BRIGHT_RED 9
#define BRIGHT_WHITE 15

int screen;

/* A new screen: cleared, white on black, its number and what to see. */
void start(char *what)
{
    vdp_reset_graphics();
    vdp_reset_viewports();
    vdp_cls();
    screen++;
    vdp_colour(WHITE);
    printf("SCREEN %d\r\n%s\r\n", screen, what);
}

/* Wait at the bottom of the screen for N (other keys, such as those of
 * switching windows to report, are ignored); Escape ends the tour. */
void next(void)
{
    int k;

    vdp_tab(0, 28);
    vdp_colour(WHITE);
    printf("N: next screen   Esc: stop");
    for (;;) {
        k = mos_getkey();
        if (k == 'n' || k == 'N')
            return;
        if (k == 27) {
            vdp_cls();
            printf("Tour stopped at screen %d.\r\n", screen);
            exit(0);
        }
    }
}

/* Wait about n centiseconds by MOS's clock. */
void pause(int n)
{
    unsigned long t;

    t = MOS_SYSVAR->time;
    while (MOS_SYSVAR->time - t < (unsigned long)n)
        ;
}

void text_screen(void)
{
    start("Line 1: RED, GREEN, BLUE each in its colour.\r\n"
          "Line 2: BLACK ON YELLOW (black text, yellow background).\r\n"
          "Line 3: 'X' at the far right of the line, column 70.\r\n"
          "Line 4: a heart-like shape, character 1 drawn literally.");
    vdp_tab(0, 6);
    vdp_colour(RED);
    printf("RED ");
    vdp_colour(GREEN);
    printf("GREEN ");
    vdp_set_text_colour(BLUE);
    printf("BLUE");
    vdp_tab(0, 7);
    vdp_colour(BLACK);
    vdp_colour(128 + YELLOW);
    printf("BLACK ON YELLOW");
    vdp_colour(128 + BLACK);
    vdp_colour(WHITE);
    vdp_cursor_tab(70, 8);
    printf("X");
    vdp_tab(0, 9);
    vdp_redefine_character_special(1, 0x00, 0x66, 0xFF, 0xFF, 0x7E, 0x3C, 0x18, 0x00);
    vdp_literal(1);
    next();
}

void lines_screen(void)
{
    start("A white rectangle outline around the lower part of the screen,\r\n"
          "a red diagonal from its bottom-left to top-right corner,\r\n"
          "and a thick green horizontal line across the middle\r\n"
          "(thick needs VDP 2.6; on older VDPs it is thin).");
    vdp_gcol(0, WHITE);
    vdp_move(100, 100);
    vdp_draw(1180, 100);
    vdp_draw(1180, 700);
    vdp_draw(100, 700);
    vdp_draw(100, 100);
    vdp_gcol(0, RED);
    vdp_move_to(100, 100);
    vdp_line_to(1180, 700);
    vdp_gcol(0, GREEN);
    vdp_set_line_thickness(5);
    vdp_move_to(100, 400);
    vdp_line_to(1180, 400);
    vdp_set_line_thickness(1);
    next();
}

/* The top row through our functions, the bottom row through vdp_plot's
 * raw codes. (AgDev's codes for these, 80 and 148, drew nothing here:
 * "move" codes; ours now draw, 85 and 149.) */
void shapes_screen(void)
{
    start("TOP ROW (our functions) and BOTTOM ROW (plain PLOT codes):\r\n"
          "left a filled red rectangle, middle a filled green triangle,\r\n"
          "right a blue circle. The rows should match; note any shape\r\n"
          "missing from the TOP row.");
    vdp_gcol(0, RED);
    vdp_move_to(100, 500);
    vdp_filled_rect(350, 700);
    vdp_gcol(0, GREEN);
    vdp_move_to(500, 500);
    vdp_move_to(750, 500);
    vdp_triangle(625, 700);
    vdp_gcol(0, BLUE);
    vdp_move_to(1000, 600);
    vdp_circle(1100, 600);
    vdp_gcol(0, RED);
    vdp_plot(4, 100, 150);
    vdp_plot(101, 350, 350);            /* rectangle, absolute, foreground */
    vdp_gcol(0, GREEN);
    vdp_plot(4, 500, 150);
    vdp_plot(4, 750, 150);
    vdp_plot(85, 625, 350);             /* triangle, absolute, foreground */
    vdp_gcol(0, BLUE);
    vdp_plot(4, 1000, 250);
    vdp_plot(149, 1100, 250);           /* circle outline, absolute, foreground */
    next();
}

void palette_screen(void)
{
    start("The word BLUE is bright blue: colour 9 (normally bright red)\r\n"
          "redefined. The word RED below it is red: colour 1, unchanged.\r\n"
          "(Redefining a colour recolours what is already drawn in it.)");
    vdp_define_colour(BRIGHT_RED, 255, 0, 0, 255);
    vdp_tab(0, 6);
    vdp_colour(BRIGHT_RED);
    printf("BLUE");
    vdp_tab(0, 8);
    vdp_colour(RED);
    printf("RED");
    next();
}

void viewport_screen(void)
{
    int i;

    start("A box six lines high at the right fills with numbered lines;\r\n"
          "once full, each new line pushes the others up, so it ends\r\n"
          "showing lines 15 to 20. The text on the left never moves.");
    vdp_tab(0, 8);
    printf("This text stays put.");
    vdp_set_text_viewport(50, 13, 75, 8);
    vdp_colour(YELLOW);
    for (i = 1; i <= 20; i++) {
        printf(i == 1 ? "line %d" : "\r\nline %d", i);
        pause(30);
    }
    vdp_reset_viewports();
    next();
}

void bitmap_screen(void)
{
    int x;

    start("A red square near the top left (a bitmap drawn once) and a\r\n"
          "cyan square that glides from left to right (a sprite), then\r\n"
          "disappears when it is hidden.");
    vdp_select_bitmap(0);
    vdp_solid_bitmap(32, 32, 255, 0, 0, 255);
    vdp_draw_bitmap(40, 100);
    vdp_select_bitmap(1);
    vdp_solid_bitmap(24, 24, 0, 255, 255, 255);
    vdp_create_sprite(0, 1, 1);
    vdp_activate_sprites(1);
    vdp_show_sprite();
    for (x = 0; x < 560; x = x + 8) {
        vdp_move_sprite_to(x, 300);
        vdp_refresh_sprites();
        pause(2);
    }
    vdp_hide_sprite();
    vdp_refresh_sprites();
    next();
    vdp_reset_sprites();
}

/* Buffer 100 holds the VDU bytes for a filled rectangle at the origin; it
 * is replayed at three origins. */
void buffer_screen(void)
{
    static char square[15] = {
        18, 0, MAGENTA,                 /* GCOL 0, magenta */
        25, 4, 0, 0, 0, 0,              /* PLOT 4, 0, 0 */
        25, 101, 200, 0, 200, 0         /* PLOT 101, 200, 200: filled to there */
    };
    int i;

    start("Three magenta squares in a row across the middle, drawn by\r\n"
          "running one stored buffer three times.");
    vdp_adv_clear_buffer(100);
    vdp_adv_write_block_data(100, 15, square);
    for (i = 0; i < 3; i++) {
        vdp_graphics_origin(150 + 350 * i, 400);
        vdp_adv_call_buffer(100);
    }
    vdp_graphics_origin(0, 0);
    next();
}

/* The contexts are untested on real hardware. On the emulator (its VDP,
 * 2026-10-03) the background colour set after a save (line 2) did not
 * show, perhaps an emulator bug: the bytes are as documented, so this
 * screen describes the documented result. */
void context_screen(void)
{
    start("Line 1: FIRST in dim blue, then SECOND in bright white: the\r\n"
          "colour is saved, changed, and restored (needs VDP 2.8).\r\n"
          "Line 2: BAND in black on a bright white band, set after a\r\n"
          "save, then restored. (The emulator showed nothing here.)");
    vdp_tab(0, 7);
    vdp_colour(BRIGHT_WHITE);
    vdp_context_save();
    vdp_colour(BLUE);
    printf("FIRST");
    vdp_context_restore();
    printf(" SECOND");
    vdp_tab(0, 9);
    vdp_context_save();
    vdp_colour(BLACK);
    vdp_colour(128 + BRIGHT_WHITE);
    printf("BAND");
    vdp_context_restore();
    next();
}

void request_screen(void)
{
    unsigned char cx;
    unsigned char cy;
    long colour;

    cx = 0;
    cy = 0;

    start("Answers read back from the VDP:\r\n"
          "the cursor line says 10, 7 and the pixel line says ff0000\r\n"
          "(the bright red square drawn at the right).");
    vdp_gcol(0, BRIGHT_RED);
    vdp_move_to(1000, 300);
    vdp_filled_rect(1200, 500);
    vdp_tab(10, 7);
    if (vdp_return_text_cursor_position(&cx, &cy) < 0)
        printf("(no answer)");
    vdp_tab(0, 9);
    printf("cursor: %d, %d", cx, cy);
    colour = vdp_return_pixel_colour(1100, 400);
    vdp_tab(0, 10);
    printf("pixel: %lx", colour);
    next();
}

void sound_screen(void)
{
    start("Sound: three short notes rising, then one low note fading\r\n"
          "away slowly (a volume envelope).");
    vdp_audio_play_note(0, 100, 440, 300);
    pause(40);
    vdp_audio_play_note(0, 100, 554, 300);
    pause(40);
    vdp_audio_play_note(0, 100, 659, 300);
    pause(40);
    vdp_audio_volume_envelope_ADSR(1, 10, 100, 100, 1500);
    vdp_audio_play_note(1, 100, 220, 200);
    pause(200);
    vdp_audio_volume_envelope_disable(1);
    next();
}

int main(void)
{
    vdp_mode(0);
    vdp_cursor(0);
    text_screen();
    lines_screen();
    shapes_screen();
    palette_screen();
    viewport_screen();
    bitmap_screen();
    buffer_screen();
    context_screen();
    request_screen();
    sound_screen();
    vdp_cursor(1);
    vdp_cls();
    printf("Tour finished: %d screens.\r\n", screen);
    return 0;
}
