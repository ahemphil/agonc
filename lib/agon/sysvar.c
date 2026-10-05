/* sysvar.c - <agon/mos.h>'s getsysvar_ functions: one system variable
 * each, read from MOS's block (MOS_SYSVAR). They exist so that programs
 * written for AgDev, which has these names, build unchanged; new code can
 * use MOS_SYSVAR->field directly. In libagon.s.
 *
 * MOS_SYSVAR is mos_sysvars() (one MOS call) cast to a pointer to
 * volatile struct mos_sysvar, so each read here really loads the byte
 * MOS last wrote rather than a copy the compiler kept. Fields wider than
 * a byte (time, scr_width, keydelay, ...) are little-endian, as MOS
 * stores them and as agonc reads them.
 */

#include <agon/mos.h>

unsigned long getsysvar_time(void) { return MOS_SYSVAR->time; }
int getsysvar_vdp_pflags(void) { return MOS_SYSVAR->vdp_pflags; }
int getsysvar_keyascii(void) { return MOS_SYSVAR->keyascii; }
int getsysvar_keymods(void) { return MOS_SYSVAR->keymods; }
int getsysvar_cursorX(void) { return MOS_SYSVAR->cursor_x; }
int getsysvar_cursorY(void) { return MOS_SYSVAR->cursor_y; }
int getsysvar_scrchar(void) { return MOS_SYSVAR->scrchar; }

/* The pixel read by VDU 23,0,&84 as R, B, G bytes, R lowest: the raw
 * byte order MOS keeps (vdpsys.c's pixel() turns it into 0xRRGGBB). The
 * pointer is fetched once, so the three bytes cost one MOS call. */
unsigned int getsysvar_scrpixel(void)
{
    volatile struct mos_sysvar *v;

    v = MOS_SYSVAR;
    return v->scrpixel[0] | v->scrpixel[1] << 8 | (unsigned int)v->scrpixel[2] << 16;
}

int getsysvar_audioChannel(void) { return MOS_SYSVAR->audio_channel; }
int getsysvar_audioSuccess(void) { return MOS_SYSVAR->audio_success; }
unsigned int getsysvar_scrwidth(void) { return MOS_SYSVAR->scr_width; }
unsigned int getsysvar_scrheight(void) { return MOS_SYSVAR->scr_height; }
int getsysvar_scrCols(void) { return MOS_SYSVAR->scr_cols; }
int getsysvar_scrRows(void) { return MOS_SYSVAR->scr_rows; }
int getsysvar_scrColours(void) { return MOS_SYSVAR->scr_colours; }
int getsysvar_scrpixelIndex(void) { return MOS_SYSVAR->scrpixel_index; }
int getsysvar_vkeycode(void) { return MOS_SYSVAR->vkeycode; }
int getsysvar_vkeydown(void) { return MOS_SYSVAR->vkeydown; }
int getsysvar_vkeycount(void) { return MOS_SYSVAR->vkeycount; }

/* The clock's six bytes (mos_setrtc's layout); mos_getrtc refreshes them. */
volatile unsigned char *getsysvar_rtc(void) { return MOS_SYSVAR->rtc; }

unsigned int getsysvar_keydelay(void) { return MOS_SYSVAR->keydelay; }
unsigned int getsysvar_keyrate(void) { return MOS_SYSVAR->keyrate; }
int getsysvar_keyled(void) { return MOS_SYSVAR->keyled; }
