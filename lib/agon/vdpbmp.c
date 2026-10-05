/* vdpbmp.c - <agon/vdp.h>'s bitmap and sprite commands, VDU 23, 27, n.
 * In libagon.s.
 *
 * A bitmap with an 8-bit ID n lives in VDP buffer 64000+n; the commands
 * with "adv" in their names take a 16-bit buffer ID instead (Console8 VDP
 * 2.2.0 and later). Sprites, up to 256, are numbered from 0 and position
 * by pixels, not the graphics coordinates PLOT uses.
 *
 * Every command starts VDU 23, 27, cmd (vdp.c's opening comment explains
 * the protocol). The bitmap commands act on the selected bitmap and the
 * sprite commands on the selected sprite, so a typical sequence selects
 * first: select bitmap n, load its pixels; select sprite m, clear it, add
 * bitmaps as frames, show it, activate the sprites.
 */

#include <agon/vdp.h>
#include <agon/mos.h>

/* A 16-bit value, low byte first (as in vdp.c). */
#define W(b, i, v) ((b)[i] = (char)((v) & 255), (b)[(i) + 1] = (char)((v) >> 8 & 255))

/* VDU 23, 27, cmd and n (0-2) more bytes. */
static void bmp(int cmd, int n, int a, int b)
{
    char x[5];

    x[0] = 23;
    x[1] = 27;
    x[2] = cmd;
    x[3] = a;
    x[4] = b;
    vdu_n(x, 3 + n);
}

/* VDU 23, 27, cmd and n (0-2) 16-bit values. */
static void bmp_words(int cmd, int n, int a, int b)
{
    char x[7];

    x[0] = 23;
    x[1] = 27;
    x[2] = cmd;
    W(x, 3, a);
    W(x, 5, b);
    vdu_n(x, 3 + 2 * n);
}

/* ---- bitmaps ------------------------------------------------------------------------------ */

void vdp_select_bitmap(int n)
{
    bmp(0, 1, n, 0);
}

/* Pixels into the selected bitmap: width * height of them, four bytes
 * each, red, green, blue, alpha, rows from the top. (A buffer and
 * vdp_adv_bitmap_from_buffer take less time and allow smaller formats.)
 * VDU 23, 27, 1, width; height; then the pixel bytes, which go in
 * 4096-byte pieces, each through vdu_n and so its Ctrl-C check. */
void vdp_load_bitmap(int width, int height, const unsigned long *data)
{
    long n;
    const char *p;

    bmp_words(1, 2, width, height);
    n = (long)width * height * 4;
    p = (const char *)data;
    while (n > 0) {
        vdu_n(p, n > 4096 ? 4096 : (int)n);
        p = p + 4096;
        n = n - 4096;
    }
}

/* The same from a file of RGBA8888 pixels: 0, or -1 if it cannot be read
 * (what was sent stays sent). The file is read through a 256-byte buffer
 * on the stack, each piece sent as it arrives, so no RAM the size of the
 * bitmap is needed. A file shorter than width * height * 4 bytes leaves
 * the VDP still expecting pixel data, which it takes from whatever is
 * sent next. */
int vdp_load_bitmap_file(const char *filename, int width, int height)
{
    char buf[256];
    long n;
    unsigned int got;
    int h;

    h = mos_fopen(filename, FA_READ);
    if (h == 0)
        return -1;
    bmp_words(1, 2, width, height);
    n = (long)width * height * 4;
    while (n > 0) {
        got = mos_fread(h, buf, n > 256 ? 256 : (unsigned int)n);
        if (got == 0)
            break;
        vdu_n(buf, (int)got);
        n = n - got;
    }
    mos_fclose(h);
    return n > 0 ? -1 : 0;
}

/* The screen between the last two graphics points into bitmap n (VDP
 * 2.2.0: VDU 23, 27, 1, n, 0, 0;): seven bytes, the last two a 16-bit
 * zero. The command number is the pixel load's; these arguments make it
 * a capture. */
void vdp_capture_bitmap(int n)
{
    char x[7];

    x[0] = 23;
    x[1] = 27;
    x[2] = 1;
    x[3] = n;
    x[4] = 0;
    x[5] = 0;
    x[6] = 0;
    vdu_n(x, 7);
}

/* A width by height bitmap of one colour, in the selected bitmap; each
 * component 0-255. VDU 23, 27, 2, width; height; r, g, b, a. */
void vdp_solid_bitmap(int width, int height, int r, int g, int b, int a)
{
    char x[11];

    x[0] = 23;
    x[1] = 27;
    x[2] = 2;
    W(x, 3, width);
    W(x, 5, height);
    x[7] = r;
    x[8] = g;
    x[9] = b;
    x[10] = a;
    vdu_n(x, 11);
}

/* The selected bitmap at pixel x, y, its top left corner, outside the
 * viewport rules; PLOT &E8-&EF (vdp_plot) is the better way. */
void vdp_draw_bitmap(int x, int y)
{
    bmp_words(3, 2, x, y);
}

/* VDU 23, 27, &20, buffer;: the bitmap in a buffer, by its 16-bit ID. */
void vdp_adv_select_bitmap(int buffer)
{
    bmp_words(0x20, 1, buffer, 0);
}

/* The selected buffer as a bitmap. format: 0 RGBA8888, 1 RGBA2222, 2
 * mono (drawn in the graphics colour). */
void vdp_adv_bitmap_from_buffer(int width, int height, int format)
{
    char x[8];

    x[0] = 23;
    x[1] = 27;
    x[2] = 0x21;
    W(x, 3, width);
    W(x, 5, height);
    x[7] = format;
    vdu_n(x, 8);
}

/* The screen between the last two graphics points into a buffer: VDU
 * 23, 27, &21, buffer; 0; (the same command number as
 * vdp_adv_bitmap_from_buffer, with a buffer ID and a zero instead). */
void vdp_adv_capture_bitmap(int buffer)
{
    bmp_words(0x21, 2, buffer, 0);
}

/* ---- sprites ---------------------------------------------------------------------------------- */

/* VDU 23, 27, 4 to 21 (and &26, &35 for the 16-bit IDs), most acting on
 * the sprite vdp_select_sprite chose. */
void vdp_select_sprite(int n)
{
    bmp(4, 1, n, 0);
}

void vdp_clear_sprite(void)
{
    bmp(5, 0, 0, 0);
}

/* Bitmap n (8-bit ID) as the selected sprite's next frame. */
void vdp_add_sprite_bitmap(int n)
{
    bmp(6, 1, n, 0);
}

/* Sprites 0 to n-1 are drawn (unless hidden). */
void vdp_activate_sprites(int n)
{
    bmp(7, 1, n, 0);
}

void vdp_next_sprite_frame(void)
{
    bmp(8, 0, 0, 0);
}

void vdp_prev_sprite_frame(void)
{
    bmp(9, 0, 0, 0);
}

void vdp_nth_sprite_frame(int n)
{
    bmp(10, 1, n, 0);
}

void vdp_show_sprite(void)
{
    bmp(11, 0, 0, 0);
}

void vdp_hide_sprite(void)
{
    bmp(12, 0, 0, 0);
}

void vdp_move_sprite_to(int x, int y)
{
    bmp_words(13, 2, x, y);
}

void vdp_move_sprite_by(int x, int y)
{
    bmp_words(14, 2, x, y);
}

/* Software sprites move on the screen at the next drawing, or now. */
void vdp_refresh_sprites(void)
{
    bmp(15, 0, 0, 0);
}

/* Bitmaps and sprites both. */
void vdp_reset_sprites(void)
{
    bmp(16, 0, 0, 0);
}

void vdp_reset_sprites_only(void)
{
    bmp(17, 0, 0, 0);
}

/* The GCOL paint mode the selected sprite is drawn with (VDP 2.6.0). */
void vdp_set_sprite_paint_mode(int mode)
{
    bmp(18, 1, mode, 0);
}

/* A hardware sprite (VDP 2.12.0; RGBA8888 or RGBA2222 frames only). */
void vdp_set_sprite_hardware(void)
{
    bmp(19, 0, 0, 0);
}

void vdp_set_sprite_software(void)
{
    bmp(20, 0, 0, 0);
}

/* Bitmap n in place of the selected sprite's current frame (VDP 2.12.0). */
void vdp_replace_sprite_frame(int n)
{
    bmp_words(21, 1, n, 0);
}

void vdp_adv_add_sprite_bitmap(int buffer)
{
    bmp_words(0x26, 1, buffer, 0);
}

void vdp_adv_replace_sprite_frame(int buffer)
{
    bmp_words(0x35, 1, buffer, 0);
}

/* Sprite n with frames from bitmaps first to first+frames-1 (8-bit IDs),
 * left selected. A convenience with no VDU command of its own: select,
 * clear, then one add per frame. */
void vdp_create_sprite(int sprite, int first, int frames)
{
    int i;

    vdp_select_sprite(sprite);
    vdp_clear_sprite();
    for (i = 0; i < frames; i++)
        vdp_add_sprite_bitmap(first + i);
}

/* The same with 16-bit buffer IDs. */
void vdp_adv_create_sprite(int sprite, int first, int frames)
{
    int i;

    vdp_select_sprite(sprite);
    vdp_clear_sprite();
    for (i = 0; i < frames; i++)
        vdp_adv_add_sprite_bitmap(first + i);
}

/* The selected bitmap as a mouse cursor, its hot spot at hot_x, hot_y;
 * select it with vdp_mouse_set_cursor and the bitmap's buffer ID. */
void vdp_mouse_cursor_from_bitmap(int hot_x, int hot_y)
{
    bmp(0x40, 2, hot_x, hot_y);
}
