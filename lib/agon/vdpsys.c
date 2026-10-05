/* vdpsys.c - <agon/vdp.h>'s system commands, VDU 23, 0, n: the cursor's
 * shape, the keyboard, the mouse, characters and fonts, viewports from
 * graphics coordinates, VDP variables and modes, and the requests whose
 * answers come back through MOS's system variables. In libagon.s.
 *
 * A request clears its bit in MOS's VDP reply flags, sends the command,
 * and (if asked to wait) waits for the VDP's reply to set the bit again,
 * at most a second by MOS's clock, so a VDP that never answers (an old
 * VDP, or none) cannot hang the program. MOS 2.3.3 has no call for
 * clearing a flag, so the request clears it in the system variables
 * itself, with interrupts off for that moment: MOS's handler sets the
 * other bits from an interrupt.
 *
 * The round trip, step by step: the program sends, say, VDU 23, 0, &82
 * (where is the text cursor?); the VDP answers with a packet up the
 * serial link; MOS's UART0 interrupt decodes it, stores the column and
 * row in cursor_x and cursor_y and sets VDP_PFLAG_CURSOR in vdp_pflags.
 * The program sees only the system variables change. The "request"
 * functions stop after sending (or after the wait), the "return" ones
 * also read the answer out of the system variables.
 *
 * Every command here starts VDU 23, 0 (vdp.c's opening comment explains
 * the protocol). sys and sys_words build the short fixed-length ones;
 * the rest build their bytes inline.
 */

#include <agon/vdp.h>
#include <agon/mos.h>

/* A 16-bit value, low byte first (as in vdp.c). */
#define W(b, i, v) ((b)[i] = (char)((v) & 255), (b)[(i) + 1] = (char)((v) >> 8 & 255))

/* VDU 23, 0, cmd, then n further bytes from the arguments (n 0-3; the
 * unused arguments are stored but not sent). */
static void sys(int cmd, int n, int a, int b, int c)
{
    char x[6];

    x[0] = 23;
    x[1] = 0;
    x[2] = cmd;
    x[3] = a;
    x[4] = b;
    x[5] = c;
    vdu_n(x, 3 + n);
}

/* VDU 23, 0, cmd, then n (0-2) 16-bit values. */
static void sys_words(int cmd, int n, int a, int b)
{
    char x[7];

    x[0] = 23;
    x[1] = 0;
    x[2] = cmd;
    W(x, 3, a);
    W(x, 5, b);
    vdu_n(x, 3 + 2 * n);
}

/* Sends cmd (n bytes) after clearing reply flag bit; if wait, waits for
 * the bit to come back: 0 once it has (or if not waiting), -1 after a
 * second without it. Shared with vdpaudio.c (the audio status), hence
 * the external name in the implementation's __ namespace.
 *
 * The flag is cleared before the command is sent, not after: a fast
 * reply could otherwise arrive first and have its bit wiped out, and the
 * wait would run to its timeout. Clearing is a read-modify-write of a
 * byte MOS's interrupt also writes, so it runs between DI and EI: an
 * interrupt between the load and the store could set another request's
 * bit, and the store would then lose it. */
int __vdp_ask(const char *cmd, int n, int bit, int wait)
{
    volatile struct mos_sysvar *v;
    unsigned long start;

    v = MOS_SYSVAR;
    asm("di");
    v->vdp_pflags = v->vdp_pflags & ~bit;
    asm("ei");
    vdu_n(cmd, n);
    if (!wait)
        return 0;
    /* time counts centiseconds, so 100 is a second. The difference is
     * unsigned, so it stays right when the counter wraps. */
    start = v->time;
    while (!(v->vdp_pflags & bit))
        if (v->time - start > 100)
            return -1;
    return 0;
}

/* VDU 23, 0, cmd and two 16-bit values, as a request. */
static int ask_xy(int cmd, int x, int y, int bit, int wait)
{
    char b[7];

    b[0] = 23;
    b[1] = 0;
    b[2] = cmd;
    W(b, 3, x);
    W(b, 5, y);
    return __vdp_ask(b, 7, bit, wait);
}

/* The pixel colour MOS last received, as 0xRRGGBB (MOS keeps R, B, G:
 * scrpixel[0] red, [1] blue, [2] green, so the bytes are reordered). */
static long pixel(void)
{
    volatile struct mos_sysvar *v;

    v = MOS_SYSVAR;
    return (long)v->scrpixel[0] << 16 | (long)v->scrpixel[2] << 8 | v->scrpixel[1];
}

/* ---- the text cursor ------------------------------------------------------------------- */

/* VDU 23, 0, &0A and &0B set the cursor's start and end rows, &8A and
 * &8B its start and end columns, &8C moves it by x, y pixels. */
void vdp_set_cursor_start_line(int n)
{
    sys(0x0A, 1, n, 0, 0);
}

void vdp_set_cursor_end_line(int n)
{
    sys(0x0B, 1, n, 0, 0);
}

void vdp_set_cursor_start_column(int n)
{
    sys(0x8A, 1, n, 0, 0);
}

void vdp_set_cursor_end_column(int n)
{
    sys(0x8B, 1, n, 0, 0);
}

void vdp_move_cursor_relative(int x, int y)
{
    sys_words(0x8C, 2, x, y);
}

/* ---- requests: the answers arrive in MOS's system variables ------------------------------ */

/* VDU 23, 0, &80, n: the VDP answers with n, into MOS_SYSVAR->gp. This
 * one only sends; it does not wait for the answer. */
void vdp_general_poll(int n)
{
    sys(0x80, 1, n, 0, 0);
}

/* VDU 23, 0, &82: the text cursor's column and row. */
void vdp_request_text_cursor_position(int wait)
{
    char b[3];

    b[0] = 23;
    b[1] = 0;
    b[2] = 0x82;
    __vdp_ask(b, 3, VDP_PFLAG_CURSOR, wait);
}

/* The text cursor's column and row into *x and *y: 0, or -1 (and *x and
 * *y unchanged) if the VDP did not say. */
int vdp_return_text_cursor_position(unsigned char *x, unsigned char *y)
{
    char b[3];

    b[0] = 23;
    b[1] = 0;
    b[2] = 0x82;
    if (__vdp_ask(b, 3, VDP_PFLAG_CURSOR, 1) < 0)
        return -1;
    *x = MOS_SYSVAR->cursor_x;
    *y = MOS_SYSVAR->cursor_y;
    return 0;
}

/* VDU 23, 0, &83, x; y;: the character at text column x, row y. */
void vdp_request_ascii_code_at_position(int x, int y, int wait)
{
    ask_xy(0x83, x, y, VDP_PFLAG_SCRCHAR, wait);
}

/* The character at text column x, row y; 0 if it is not recognised, -1 if
 * the VDP did not say. */
int vdp_return_ascii_code_at_position(int x, int y)
{
    if (ask_xy(0x83, x, y, VDP_PFLAG_SCRCHAR, 1) < 0)
        return -1;
    return MOS_SYSVAR->scrchar;
}

/* VDU 23, 0, &93, x; y;: the same at graphics coordinates; the answer
 * comes back as for &83. */
void vdp_request_ascii_code_at_graphics_position(int x, int y, int wait)
{
    ask_xy(0x93, x, y, VDP_PFLAG_SCRCHAR, wait);
}

int vdp_return_ascii_code_at_graphics_position(int x, int y)
{
    if (ask_xy(0x93, x, y, VDP_PFLAG_SCRCHAR, 1) < 0)
        return -1;
    return MOS_SYSVAR->scrchar;
}

/* VDU 23, 0, &84, x; y;: the colour of the pixel at graphics x, y. */
void vdp_request_pixel_colour(int x, int y, int wait)
{
    ask_xy(0x84, x, y, VDP_PFLAG_POINT, wait);
}

/* The colour at graphics x, y as 0xRRGGBB; -1 if the VDP did not say. */
long vdp_return_pixel_colour(int x, int y)
{
    if (ask_xy(0x84, x, y, VDP_PFLAG_POINT, 1) < 0)
        return -1;
    return pixel();
}

/* VDU 23, 0, &94, n: palette entry n. The answer comes back as a pixel
 * read's does (scrpixel and scrpixel_index, VDP_PFLAG_POINT). */
void vdp_request_palette_entry(int n, int wait)
{
    char b[4];

    b[0] = 23;
    b[1] = 0;
    b[2] = 0x94;
    b[3] = n;
    __vdp_ask(b, 4, VDP_PFLAG_POINT, wait);
}

/* Palette entry n (0-63, or 128-131 for the current colours) as 0xRRGGBB;
 * -1 if the VDP did not say. The request's own result is not used: after
 * a wait the flag itself says whether the answer came. */
long vdp_return_palette_entry_colour(int n)
{
    vdp_request_palette_entry(n, 1);
    if (!(MOS_SYSVAR->vdp_pflags & VDP_PFLAG_POINT))
        return -1;
    return pixel();
}

/* The colour number behind palette entry n; -1 if the VDP did not say. */
int vdp_return_palette_entry_index(int n)
{
    vdp_request_palette_entry(n, 1);
    if (!(MOS_SYSVAR->vdp_pflags & VDP_PFLAG_POINT))
        return -1;
    return MOS_SYSVAR->scrpixel_index;
}

/* VDU 23, 0, &86: the screen's size and mode, into the system
 * variables. */
void vdp_get_scr_dims(int wait)
{
    char b[3];

    b[0] = 23;
    b[1] = 0;
    b[2] = 0x86;
    __vdp_ask(b, 3, VDP_PFLAG_MODE, wait);
}

/* VDU 23, 0, &87, 0: the clock, into the system variables (mos_getrtc
 * reads it as text). &87, 1 sets it (vdp_set_rtc below). */
void vdp_request_rtc(int wait)
{
    char b[4];

    b[0] = 23;
    b[1] = 0;
    b[2] = 0x87;
    b[3] = 0;
    __vdp_ask(b, 4, VDP_PFLAG_RTC, wait);
}

/* Set the clock: the year less 1980, month, day, hour, minute, second. */
void vdp_set_rtc(int year, int month, int day, int hour, int minute, int second)
{
    char b[10];

    b[0] = 23;
    b[1] = 0;
    b[2] = 0x87;
    b[3] = 1;
    b[4] = year;
    b[5] = month;
    b[6] = day;
    b[7] = hour;
    b[8] = minute;
    b[9] = second;
    vdu_n(b, 10);
}

/* ---- the keyboard ----------------------------------------------------------------------- */

/* VDU 23, 0, &81, locale (vdp.h lists them). */
void vdp_set_keyboard_locale(int locale)
{
    sys(0x81, 1, locale, 0, 0);
}

/* VDU 23, 0, &88, delay; rate; led: repeat delay and rate in
 * milliseconds, and the lights (bit 0 Scroll Lock, 1 Caps Lock, 2 Num
 * Lock). */
void vdp_keyboard_control(int delay, int rate, int led)
{
    char b[8];

    b[0] = 23;
    b[1] = 0;
    b[2] = 0x88;
    W(b, 3, delay);
    W(b, 5, rate);
    b[7] = led;
    vdu_n(b, 8);
}

/* AgDev's spelling. */
void vdp_keyboard_cotrol(int delay, int rate, int led)
{
    vdp_keyboard_control(delay, rate, led);
}

/* VDU 23, 0, &98, 0 or 1: whether Ctrl+letter keys act on the VDP. */
void vdp_control_keys(int on)
{
    sys(0x98, 1, on != 0, 0, 0);
}

/* A fresh keyboard packet for one key, by its FabGL virtual key code. */
void vdp_request_key_state(int vkey)
{
    sys(0x99, 1, vkey, 0, 0);
}

/* ---- the mouse ---------------------------------------------------------------------------- */

/* All VDU 23, 0, &89, sub: 0 enable, 1 disable, 2 reset, 3 cursor, 4
 * position, 6 sample rate, 7 resolution, 8 scaling, 9 acceleration, 10
 * wheel acceleration; the arguments follow sub. */
void vdp_mouse_enable(void)
{
    sys(0x89, 1, 0, 0, 0);
}

void vdp_mouse_disable(void)
{
    sys(0x89, 1, 1, 0, 0);
}

void vdp_mouse_reset(void)
{
    sys(0x89, 1, 2, 0, 0);
}

/* 0-18 the system cursors, a bitmap ID for one made with
 * vdp_mouse_cursor_from_bitmap, 65535 to hide it. */
void vdp_mouse_set_cursor(int cursor)
{
    char b[6];

    b[0] = 23;
    b[1] = 0;
    b[2] = 0x89;
    b[3] = 3;
    W(b, 4, cursor);
    vdu_n(b, 6);
}

void vdp_mouse_set_position(int x, int y)
{
    char b[8];

    b[0] = 23;
    b[1] = 0;
    b[2] = 0x89;
    b[3] = 4;
    W(b, 4, x);
    W(b, 6, y);
    vdu_n(b, 8);
}

void vdp_mouse_sample_rate(int rate)
{
    sys(0x89, 2, 6, rate, 0);
}

void vdp_mouse_resolution(int resolution)
{
    sys(0x89, 2, 7, resolution, 0);
}

void vdp_mouse_scaling(int scaling)
{
    sys(0x89, 2, 8, scaling, 0);
}

void vdp_mouse_acceleration(int acceleration)
{
    char b[6];

    b[0] = 23;
    b[1] = 0;
    b[2] = 0x89;
    b[3] = 9;
    W(b, 4, acceleration);
    vdu_n(b, 6);
}

/* A 24-bit value: three bytes, low first. */
void vdp_mouse_wheel_accel(long acceleration)
{
    char b[7];

    b[0] = 23;
    b[1] = 0;
    b[2] = 0x89;
    b[3] = 10;
    b[4] = (char)(acceleration & 255);
    b[5] = (char)(acceleration >> 8 & 255);
    b[6] = (char)(acceleration >> 16 & 255);
    vdu_n(b, 7);
}

/* ---- characters ---------------------------------------------------------------------------- */

/* VDU 23, 0, &90: as vdp_redefine_character, characters 0-31 included. */
void vdp_redefine_character_special(int c, int b0, int b1, int b2, int b3, int b4, int b5, int b6, int b7)
{
    char b[12];

    b[0] = 23;
    b[1] = 0;
    b[2] = 0x90;
    b[3] = c;
    b[4] = b0;
    b[5] = b1;
    b[6] = b2;
    b[7] = b3;
    b[8] = b4;
    b[9] = b5;
    b[10] = b6;
    b[11] = b7;
    vdu_n(b, 12);
}

/* The same from eight bytes at data. */
void vdp_define_character(int c, const unsigned char *data)
{
    char b[12];
    int i;

    b[0] = 23;
    b[1] = 0;
    b[2] = 0x90;
    b[3] = c;
    for (i = 0; i < 8; i++)
        b[4 + i] = data[i];
    vdu_n(b, 12);
}

/* VDU 23, 0, &91. */
void vdp_reset_system_font(void)
{
    sys(0x91, 0, 0, 0, 0);
}

/* VDU 23, 0, &92, c, bitmap;: character c drawn as a bitmap, by its
 * 16-bit ID. */
void vdp_map_char_to_bitmap(int c, int bitmap)
{
    char b[6];

    b[0] = 23;
    b[1] = 0;
    b[2] = 0x92;
    b[3] = c;
    W(b, 4, bitmap);
    vdu_n(b, 6);
}

/* ---- viewports and origin from graphics coordinates ---------------------------------------- */

/* VDU 23, 0, &9C to &9F: no arguments; the VDP takes the coordinates
 * from the last graphics points plotted. */
void vdp_set_text_viewport_via_plot(void)
{
    sys(0x9C, 0, 0, 0, 0);
}

void vdp_set_graphics_viewport_via_plot(void)
{
    sys(0x9D, 0, 0, 0, 0);
}

void vdp_set_graphics_origin_via_plot(void)
{
    sys(0x9E, 0, 0, 0, 0);
}

void vdp_move_graphics_origin_and_viewport(void)
{
    sys(0x9F, 0, 0, 0, 0);
}

/* ---- the rest ------------------------------------------------------------------------------- */

/* Each sends VDU 23, 0 and its command byte (&96, &9A, &9B, &C0, &C1,
 * &C3, &CA, &F2, &F8, &F9, &FE, &FF), then its arguments; an "on"
 * argument goes as 0 or 1. */
/* Experimental in the VDP: apply the 3x3 matrix in a buffer to drawing
 * (flags bit 0: bitmaps); 65535 removes it. */
void vdp_set_affine_transform(int flags, int buffer)
{
    char b[6];

    b[0] = 23;
    b[1] = 0;
    b[2] = 0x96;
    b[3] = flags;
    W(b, 4, buffer);
    vdu_n(b, 6);
}

void vdp_page_mode_once(void)
{
    sys(0x9A, 0, 0, 0, 0);
}

/* A buffer's bytes on the screen as characters, control codes included. */
void vdp_print_buffer(int buffer)
{
    sys_words(0x9B, 1, buffer, 0);
}

void vdp_logical_scr_dims(int on)
{
    sys(0xC0, 1, on != 0, 0, 0);
}

void vdp_legacy_modes(int on)
{
    sys(0xC1, 1, on != 0, 0, 0);
}

/* Swap the screen buffers (double-buffered modes) or wait for the next
 * frame. */
void vdp_swap(void)
{
    sys(0xC3, 0, 0, 0, 0);
}

void vdp_flush_drawing_commands(void)
{
    sys(0xCA, 0, 0, 0, 0);
}

void vdp_set_dash_pattern_length(int n)
{
    sys(0xF2, 1, n, 0, 0);
}

void vdp_set_variable(int id, int value)
{
    sys_words(0xF8, 2, id, value);
}

void vdp_clear_variable(int id)
{
    sys_words(0xF9, 1, id, 0);
}

void vdp_console_mode(int on)
{
    sys(0xFE, 1, on != 0, 0, 0);
}

void vdp_terminal_mode(void)
{
    sys(0xFF, 0, 0, 0, 0);
}
