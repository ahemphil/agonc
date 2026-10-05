/* vdp.c - <agon/vdp.h>: bytes to the VDP through MOS's VDU output
 * (RST 10h for one, RST 18h for several), and the main VDU commands, 0-31
 * and 127, built on them. In libagon.s; vdpsys.c, vdpbmp.c, vdpaudio.c,
 * vdpbuf.c and vdpmore.c have the rest of the VDU commands.
 *
 * ---- the VDU protocol, briefly ----
 * The Agon's screen, sound, keyboard and mouse belong to the VDP, an
 * ESP32 microcontroller with its own firmware, joined to the eZ80 by a
 * serial link (UART0). The eZ80 never touches video memory: everything a
 * program shows is a byte stream in the BBC Micro's VDU format, which
 * MOS passes down the link (RST 10h one byte, RST 18h a block). In that
 * stream a control code, 0-31 or 127, starts a command, and the VDP then
 * takes a fixed number of argument bytes for it: VDU 12 (clear the
 * screen) takes none, VDU 17 (text colour) one, VDU 25 (PLOT) five, the
 * plot code and two 16-bit coordinates. Any other byte is a character to
 * draw. A value wider than a byte goes little-endian, low byte first: the
 * VDU documentation writes a 16-bit value with a trailing ";" (x; y;),
 * and a 24-bit value (an offset or a length) is three bytes, low first.
 *
 * VDU 23 is the escape to everything the BBC Micro never had. VDU 23, c
 * for c 32-255 redefines character c; VDU 23, n for n 1-31 are settings
 * and further command groups (23, 1 the cursor, 23, 7 scrolling, 23, 27
 * the bitmap and sprite commands in vdpbmp.c), and VDU 23, 0, n are the
 * VDP's system commands (vdpsys.c). Two of the system commands open whole
 * families: audio is VDU 23, 0, &85 (vdpaudio.c), and the buffered
 * command API, VDU 23, 0, &A0 (vdpbuf.c), stores data and command
 * sequences on the VDP under 16-bit buffer IDs. Some commands make the VDP answer: its reply comes
 * back up the link, and MOS's serial interrupt stores it in MOS's system
 * variables (vdpsys.c's __vdp_ask waits for it).
 *
 * Each command is built in a small array and sent with one vdu_n, so it
 * costs one MOS call however many bytes it has. A value marked ";" in the
 * VDU documentation is 16 bits, sent low byte first (W below). The
 * arrays are char, so storing an int argument keeps its low 8 bits, which
 * is the byte the VDP takes.
 *
 * The tests compile these units with AGON_VDU_CAPTURE defined and their
 * own vdu and vdu_n, which record the bytes instead of sending them
 * (tests/libc_c/t_vdu.c); the library itself has no such hook.
 */

#include <agon/vdp.h>

extern char __intflag;
void __interrupted(void);

/* Each call is an interruption point for Ctrl-C (mos.c). Only vdu and
 * vdu_n check; every other function in the vdp*.c units reaches the VDP
 * through them, so it is an interruption point too. */
#define POLL() if (__intflag) __interrupted()

/* A 16-bit value into b[i] and b[i+1], low byte first. A macro, so v is
 * evaluated twice: pass it nothing with a side effect. Each vdp*.c unit
 * defines its own copy. */
#define W(b, i, v) ((b)[i] = (char)((v) & 255), (b)[(i) + 1] = (char)((v) >> 8 & 255))

#ifndef AGON_VDU_CAPTURE

/* ---- the two ways out: one byte, or a block ---- */

/* RST 10h writes the byte in A to the VDP. */
void vdu(int c)
{
    POLL();
    asm("ld a,(ix+6)\n"
        "rst.lis 10h");
}

/* RST 18h writes BC bytes from HL. RST 18h with BC = 0 would write up to
 * a delimiter instead: n <= 0 writes nothing. */
void vdu_n(const char *p, int n)
{
    POLL();
    if (n <= 0)
        return;
    asm("ld hl,(ix+6)\n"
        "ld bc,(ix+9)\n"
        "rst.lis 18h");
}

#endif

/* ---- helpers for the commands below ---- */

/* Two bytes: the command and one argument. */
static void vdu2(int a, int b)
{
    char c[2];

    c[0] = a;
    c[1] = b;
    vdu_n(c, 2);
}

/* A command followed by n of the four 16-bit values (VDU 24 sends four,
 * VDU 29 two); the values past n are built but not sent. */
static void vdu_words(int cmd, int n, int a, int b, int c, int d)
{
    char x[9];

    x[0] = cmd;
    W(x, 1, a);
    W(x, 3, b);
    W(x, 5, c);
    W(x, 7, d);
    vdu_n(x, 1 + 2 * n);
}

/* ---- the short names ----------------------------------------------------------------- */

/* The short names (vdp_mode, vdp_plot, ...); the AgDev-style names in the
 * sections below send the same bytes, several by calling these. */

void vdp_mode(int mode)
{
    vdu2(22, mode);
}

void vdp_cls(void)
{
    vdu(12);
}

void vdp_clg(void)
{
    vdu(16);
}

void vdp_colour(int c)
{
    vdu2(17, c);
}

void vdp_gcol(int mode, int c)
{
    char b[3];

    b[0] = 18;
    b[1] = mode;
    b[2] = c;
    vdu_n(b, 3);
}

void vdp_tab(int x, int y)
{
    char b[3];

    b[0] = 31;
    b[1] = x;
    b[2] = y;
    vdu_n(b, 3);
}

void vdp_cursor(int on)
{
    char b[3];

    b[0] = 23;
    b[1] = 1;
    b[2] = on != 0;
    vdu_n(b, 3);
}

/* VDU 25, k, x; y;: k is BBC BASIC's PLOT code. Its low three bits pick
 * relative or absolute coordinates and whether to move or draw, and in
 * which colour; the rest pick the shape (a line, a point, a triangle, a
 * circle, ...). vdp_move is PLOT 4, an absolute move; vdp_draw PLOT 5, an
 * absolute line in the foreground colour. */
void vdp_plot(int k, int x, int y)
{
    char b[6];

    b[0] = 25;
    b[1] = k;
    W(b, 2, x);
    W(b, 4, y);
    vdu_n(b, 6);
}

void vdp_move(int x, int y)
{
    vdp_plot(4, x, y);
}

void vdp_draw(int x, int y)
{
    vdp_plot(5, x, y);
}

/* ---- VDU 1-21 --------------------------------------------------------------------------- */

/* AgDev's names, one per control code; vdp.h says what each does. */

void vdp_send_to_printer(int c)
{
    vdu2(1, c);
}

void vdp_enable_printer(void)
{
    vdu(2);
}

void vdp_disable_printer(void)
{
    vdu(3);
}

void vdp_write_at_text_cursor(void)
{
    vdu(4);
}

void vdp_write_at_graphics_cursor(void)
{
    vdu(5);
}

void vdp_enable_screen(void)
{
    vdu(6);
}

void vdp_bell(void)
{
    vdu(7);
}

void vdp_cursor_left(void)
{
    vdu(8);
}

void vdp_cursor_right(void)
{
    vdu(9);
}

void vdp_cursor_down(void)
{
    vdu(10);
}

void vdp_cursor_up(void)
{
    vdu(11);
}

void vdp_clear_screen(void)
{
    vdu(12);
}

void vdp_carriage_return(void)
{
    vdu(13);
}

void vdp_page_mode_on(void)
{
    vdu(14);
}

void vdp_page_mode_off(void)
{
    vdu(15);
}

void vdp_clear_graphics(void)
{
    vdu(16);
}

void vdp_set_text_colour(int colour)
{
    vdu2(17, colour);
}

void vdp_set_graphics_colour(int mode, int colour)
{
    vdp_gcol(mode, colour);
}

void vdp_define_colour(int logical, int physical, int red, int green, int blue)
{
    char b[6];

    b[0] = 19;
    b[1] = logical;
    b[2] = physical;
    b[3] = red;
    b[4] = green;
    b[5] = blue;
    vdu_n(b, 6);
}

void vdp_reset_graphics(void)
{
    vdu(20);
}

void vdp_disable_screen(void)
{
    vdu(21);
}

/* ---- VDU 23, n: characters and the display -------------------------------------------- */

/* VDU 23, c, then the eight rows, top first (characters 32-255). */
void vdp_redefine_character(int c, int b0, int b1, int b2, int b3, int b4, int b5, int b6, int b7)
{
    char b[10];

    b[0] = 23;
    b[1] = c;
    b[2] = b0;
    b[3] = b1;
    b[4] = b2;
    b[5] = b3;
    b[6] = b4;
    b[7] = b5;
    b[8] = b6;
    b[9] = b7;
    vdu_n(b, 10);
}

void vdp_cursor_enable(int on)
{
    vdp_cursor(on);
}

/* VDU 23, 1, n: 0 hide, 1 show, 2 steady, 3 flashing (2 and 3: VDP
 * 2.8.0). */
void vdp_cursor_control(int n)
{
    char b[3];

    b[0] = 23;
    b[1] = 1;
    b[2] = n;
    vdu_n(b, 3);
}

/* VDU 23, 6: the dotted-line pattern, eight bytes, most significant bit
 * first. */
void vdp_set_dotted_line_pattern(int b0, int b1, int b2, int b3, int b4, int b5, int b6, int b7)
{
    char b[10];

    b[0] = 23;
    b[1] = 6;
    b[2] = b0;
    b[3] = b1;
    b[4] = b2;
    b[5] = b3;
    b[6] = b4;
    b[7] = b5;
    b[8] = b6;
    b[9] = b7;
    vdu_n(b, 10);
}

/* VDU 23, 7, extent, direction, movement. */
void vdp_scroll_screen_extent(int extent, int direction, int speed)
{
    char b[5];

    b[0] = 23;
    b[1] = 7;
    b[2] = extent;
    b[3] = direction;
    b[4] = speed;
    vdu_n(b, 5);
}

/* The whole screen (extent 1). */
void vdp_scroll_screen(int direction, int speed)
{
    vdp_scroll_screen_extent(1, direction, speed);
}

/* VDU 23, 16, setting, mask: new = (old AND mask) XOR setting. */
void vdp_cursor_behaviour(int setting, int mask)
{
    char b[4];

    b[0] = 23;
    b[1] = 16;
    b[2] = setting;
    b[3] = mask;
    vdu_n(b, 4);
}

void vdp_set_line_thickness(int pixels)
{
    char b[3];

    b[0] = 23;
    b[1] = 23;
    b[2] = pixels;
    vdu_n(b, 3);
}

/* ---- VDU 24-31, 127 --------------------------------------------------------------------- */

/* VDU 24: left, bottom, right, top in graphics units. */
void vdp_set_graphics_viewport(int left, int bottom, int right, int top)
{
    vdu_words(24, 4, left, bottom, right, top);
}

void vdp_move_to(int x, int y)
{
    vdp_plot(4, x, y);
}

void vdp_line_to(int x, int y)
{
    vdp_plot(5, x, y);
}

void vdp_point(int x, int y)
{
    vdp_plot(69, x, y);
}

/* A filled triangle from the last two points to x, y (PLOT 85, drawn in
 * the foreground colour). AgDev sends 80, a "move" code that draws
 * nothing (seen on the VDP, tests/visual/vdu_tour.c). */
void vdp_triangle(int x, int y)
{
    vdp_plot(85, x, y);
}

/* A circle about the last point, x, y relative to it giving the radius
 * (PLOT 145; AgDev's 144 only moves). */
void vdp_circle_radius(int x, int y)
{
    vdp_plot(145, x, y);
}

/* A circle about the last point through x, y (PLOT 149; AgDev's 148
 * only moves). */
void vdp_circle(int x, int y)
{
    vdp_plot(149, x, y);
}

/* A filled rectangle from the last point to x, y (PLOT 101). */
void vdp_filled_rect(int x, int y)
{
    vdp_plot(101, x, y);
}

void vdp_reset_viewports(void)
{
    vdu(26);
}

/* VDU 27, c: c drawn as a character even if it is a control code. */
void vdp_literal(int c)
{
    vdu2(27, c);
}

/* VDU 28: character columns and rows, one byte each. */
void vdp_set_text_viewport(int left, int bottom, int right, int top)
{
    char b[5];

    b[0] = 28;
    b[1] = left;
    b[2] = bottom;
    b[3] = right;
    b[4] = top;
    vdu_n(b, 5);
}

/* VDU 29, x; y;: two values of vdu_words' four. */
void vdp_graphics_origin(int x, int y)
{
    vdu_words(29, 2, x, y, 0, 0);
}

void vdp_cursor_home(void)
{
    vdu(30);
}

/* VDU 31, x, y (AgDev's order: column first). */
void vdp_cursor_tab(int x, int y)
{
    vdp_tab(x, y);
}

void vdp_backspace(void)
{
    vdu(127);
}
