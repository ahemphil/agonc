/* vdpmore.c - <agon/vdp.h>'s smaller VDU groups: graphics contexts (VDU
 * 23, 0, &C8; Console8 VDP 2.8.0), fonts (&95; 2.8.0), the copper
 * palettes (&C4; 2.12.0, behind a VDP variable), and the tile engine
 * (&C2; 2.12.0). In libagon.s.
 *
 * Each group is one VDU 23, 0 system command whose next byte picks the
 * operation (vdp.c's opening comment explains the protocol). send()
 * writes the three-byte group header and then the operation's bytes,
 * which each function builds in a small array.
 */

#include <agon/vdp.h>

/* A 16-bit value, low byte first (as in vdp.c). */
#define W(b, i, v) ((b)[i] = (char)((v) & 255), (b)[(i) + 1] = (char)((v) >> 8 & 255))

/* VDU 23, 0, group, then n bytes (two vdu_n calls). */
static void send(int group, const char *args, int n)
{
    char x[3];

    x[0] = 23;
    x[1] = 0;
    x[2] = group;
    vdu_n(x, 3);
    vdu_n(args, n);
}

/* ---- graphics contexts: the colours, modes, font and cursors as a whole ------------------ */

/* Operations 0 select, 1 delete, 2 reset, 3 save, 4 restore, 5 save a
 * copy, 6 restore all, 7 clear the stack: save and restore push and pop
 * the current context's state on a stack. */
/* VDU 23, 0, &C8, cmd and one byte if n is 1. */
static void ctx(int cmd, int n, int a)
{
    char x[2];

    x[0] = cmd;
    x[1] = a;
    send(0xC8, x, 1 + n);
}

void vdp_context_select(int context)
{
    ctx(0, 1, context);
}

void vdp_context_delete(int context)
{
    ctx(1, 1, context);
}

/* flags pick what is reset (the VDP docs list the bits). */
void vdp_context_reset(int flags)
{
    ctx(2, 1, flags);
}

void vdp_context_save(void)
{
    ctx(3, 0, 0);
}

void vdp_context_restore(void)
{
    ctx(4, 0, 0);
}

void vdp_context_save_copy(int context)
{
    ctx(5, 1, context);
}

void vdp_context_restore_all(void)
{
    ctx(6, 0, 0);
}

void vdp_context_clear_stack(void)
{
    ctx(7, 0, 0);
}

/* ---- fonts, held in buffers ------------------------------------------------------------------- */

/* The font in a buffer as the current font; 65535 the system font. */
void vdp_font_select(int buffer, int flags)
{
    char x[4];

    x[0] = 0;
    W(x, 1, buffer);
    x[3] = flags;
    send(0x95, x, 4);
}

/* The buffer's data as a font of width by height characters. */
void vdp_font_create(int buffer, int width, int height, int ascent, int flags)
{
    char x[7];

    x[0] = 1;
    W(x, 1, buffer);
    x[3] = width;
    x[4] = height;
    x[5] = ascent;
    x[6] = flags;
    send(0x95, x, 7);
}

/* One of the font's properties (field) to value. */
void vdp_font_adjust(int buffer, int field, int value)
{
    char x[6];

    x[0] = 2;
    W(x, 1, buffer);
    x[3] = field;
    W(x, 4, value);
    send(0x95, x, 6);
}

void vdp_font_delete(int buffer)
{
    char x[3];

    x[0] = 4;
    W(x, 1, buffer);
    send(0x95, x, 3);
}

/* The system font into a buffer, to change. */
void vdp_font_copy(int buffer)
{
    char x[3];

    x[0] = 5;
    W(x, 1, buffer);
    send(0x95, x, 3);
}

/* ---- the copper: palettes changed as the screen is drawn ---------------------------------------- */

/* A palette, a copy of palette 0. */
void vdp_copper_create_palette(int palette)
{
    char x[3];

    x[0] = 0;
    W(x, 1, palette);
    send(0xC4, x, 3);
}

/* 65535: all of them. */
void vdp_copper_delete_palette(int palette)
{
    char x[3];

    x[0] = 1;
    W(x, 1, palette);
    send(0xC4, x, 3);
}

/* One entry of a palette, as VDU 19 sets the main one (0-255 each). */
void vdp_copper_set_palette_entry(int palette, int index, int red, int green, int blue)
{
    char x[7];

    x[0] = 2;
    W(x, 1, palette);
    x[3] = index;
    x[4] = red;
    x[5] = green;
    x[6] = blue;
    send(0xC4, x, 7);
}

/* The signal list from a buffer: 16-bit pairs, a count of rows and the
 * palette for them. */
void vdp_copper_set_signal_list(int buffer)
{
    char x[3];

    x[0] = 3;
    W(x, 1, buffer);
    send(0xC4, x, 3);
}

void vdp_copper_reset_signal_list(void)
{
    char x[1];

    x[0] = 4;
    send(0xC4, x, 1);
}

/* ---- the tile engine ------------------------------------------------------------------------------- */

/* The operation numbers fall in groups: 0-7 the tile banks, 16-23 the
 * map, 24-30 the layer. The zero bytes some functions send are fields
 * of the documented command that this interface fixes at 0. */

/* VDU 23, 0, &C2 and n bytes of b. */
static void tile(const char *b, int n)
{
    send(0xC2, b, n);
}

void vdp_tile_bank_init(int bank)
{
    char b[5];

    b[0] = 0;
    b[1] = bank;
    b[2] = 0;
    b[3] = 0;
    b[4] = 0;
    tile(b, 5);
}

/* Tile id of a bank from 64 bytes, 8 by 8 pixels in RGBA2222. */
void vdp_tile_load(int bank, int id, const unsigned char *pixels)
{
    char b[3];

    b[0] = 1;
    b[1] = bank;
    b[2] = id;
    tile(b, 3);
    vdu_n((const char *)pixels, 64);
}

/* One tile drawn at tile position x, y plus the pixel offsets;
 * attribute bits 0 flip x, 1 flip y. */
void vdp_tile_draw(int bank, int id, int x, int y, int x_offset, int y_offset, int attribute)
{
    char b[9];

    b[0] = 6;
    b[1] = bank;
    b[2] = id;
    b[3] = 0;
    b[4] = x;
    b[5] = y;
    b[6] = x_offset;
    b[7] = y_offset;
    b[8] = attribute;
    tile(b, 9);
}

void vdp_tile_bank_free(int bank)
{
    char b[2];

    b[0] = 7;
    b[1] = bank;
    tile(b, 2);
}

/* size: 0-8 for 32x32, 32x64, 32x128, 64x32, 64x64, 64x128, 128x32,
 * 128x64, 128x128 tiles. */
void vdp_tile_map_init(int size)
{
    char b[5];

    b[0] = 16;
    b[1] = 0;
    b[2] = size;
    b[3] = 0;
    b[4] = 0;
    tile(b, 5);
}

/* attribute: bits 0 flip x, 1 flip y, 2-3 the tile bank. */
void vdp_tile_map_set(int x, int y, int id, int attribute)
{
    char b[6];

    b[0] = 17;
    b[1] = 0;
    b[2] = x;
    b[3] = y;
    b[4] = id;
    b[5] = attribute;
    tile(b, 6);
}

void vdp_tile_map_free(void)
{
    char b[2];

    b[0] = 23;
    b[1] = 0;
    tile(b, 2);
}

/* size: 0 80x60, 1 80x30, 2 40x30, 3 40x25 tiles (for 640x480, 640x240,
 * 320x240 and 320x200 modes). */
void vdp_tile_layer_init(int size)
{
    char b[5];

    b[0] = 24;
    b[1] = 0;
    b[2] = size;
    b[3] = 0;
    b[4] = 0;
    tile(b, 5);
}

/* The map position the layer shows from, and the pixel offsets. */
void vdp_tile_layer_scroll(int x, int y, int x_offset, int y_offset)
{
    char b[6];

    b[0] = 26;
    b[1] = 0;
    b[2] = x;
    b[3] = y;
    b[4] = x_offset;
    b[5] = y_offset;
    tile(b, 6);
}

void vdp_tile_layer_update(void)
{
    char b[2];

    b[0] = 28;
    b[1] = 0;
    tile(b, 2);
}

void vdp_tile_layer_draw(void)
{
    char b[2];

    b[0] = 29;
    b[1] = 0;
    tile(b, 2);
}

void vdp_tile_layer_update_draw(void)
{
    char b[2];

    b[0] = 30;
    b[1] = 0;
    tile(b, 2);
}
