/* agon/mos.h - the MOS API: every call MOS 2.3.3 provides, its FatFS
 * calls, and its system variables.
 *
 * Thin wrappers over MOS's RST 08h calls. The ones the C library itself
 * uses (files, the clock, the system variables, loading, commands) are in
 * mos.c, in libc.s; the rest are in mosapi.c and sysvar.c, in libagon.s,
 * which a program links only if it uses them. Status results are MOS's: 0
 * for success, else a FatFS result (FR_ below, 1-19) or a MOS code
 * (20-26); mos_getError gives the message. Every call is an
 * interruption point for Ctrl-C (<signal.h>) except the system-variable
 * reads, mos_getkbmap, the vector calls and the handler set-up below, and
 * agon_emu_exit. The calls MOS 3 added are not here: programs built with
 * this header run on MOS 2.3.3 and on MOS 3. Not part of C89.
 *
 * Several comments below note behaviour confirmed only on
 * fab-agon-emulator 1.2.5's host-folder SD card (the only environment
 * this project currently tests against) that traces to bugs in the
 * emulator's own convenience reimplementation of FatFs, not to MOS 2.3.3
 * or the real FatFs it ships (verified directly against both projects'
 * source). Only mos_fclose's
 * behaviour is confirmed as real MOS firmware behaviour, independent of
 * the emulator, since it never touches the disk layer at all.
 */

#ifndef _AGON_MOS_H
#define _AGON_MOS_H

/* mos_fopen's and ffs_fopen's mode bits: FA_READ, FA_WRITE or both, with
 * at most one of the others (0, opening an existing file, by default). */
#define FA_READ           0x01
#define FA_WRITE          0x02
#define FA_CREATE_NEW     0x04
#define FA_CREATE_ALWAYS  0x08
#define FA_OPEN_ALWAYS    0x10
#define FA_OPEN_APPEND    0x30

int mos_fopen(const char *filename, int mode);

/* Closing handle 0 closes every open file and returns how many remain
 * (always 0). Closing one specific handle instead just echoes that handle
 * number back - not a remaining-open count, and not an error indicator,
 * even for an invalid or already-closed handle. Confirmed as real MOS
 * 2.3.3 firmware behaviour directly from its C source (src/mos.c:
 * mos_FCLOSE's last line is unconditionally `return fh;`), not an
 * emulator artifact - neither the official docs page nor AgDev's own
 * mos_api.h comment reflects this. */
int mos_fclose(int handle);

/* Emulator-only caveat: on fab-agon-emulator 1.2.5's host-folder SD card,
 * a SHORT read (fewer than count bytes left in the file) zero-fills the
 * rest of buf up to the full count requested, even though those bytes
 * are not part of the returned count - do not assume the unread tail of
 * buf keeps its original content when testing there. This never touches
 * anything past buf+count. The real FatFs MOS 2.3.3 ships only touches
 * the bytes it actually reads, so this is not expected on real hardware. */
unsigned int mos_fread(int handle, void *buf, unsigned int count);

/* Emulator-only caveat: on fab-agon-emulator 1.2.5's host-folder SD card,
 * a handle opened with FA_OPEN_ALWAYS ignores any prior mos_flseek for
 * writes and always appends at the file's current end (a bitmask bug in
 * the emulator's f_open reimplementation). Real MOS 2.3.3's mos_FWRITE is a
 * thin wrapper over the real f_write and is not expected to have this
 * problem. Open with plain FA_WRITE instead for a write
 * that is guaranteed to honour a prior seek in both environments. */
unsigned int mos_fwrite(int handle, const void *buf, unsigned int count);

/* Positions subsequent reads correctly in every mode and environment
 * tested. See mos_fwrite above for the emulator-only caveat on writes. */
int mos_flseek(int handle, unsigned long offset);
int mos_feof(int handle);
int mos_del(const char *path);

/* Emulator-only caveat: on fab-agon-emulator 1.2.5's host-folder SD card,
 * renaming onto an EXISTING destination does not fail - it silently
 * replaces it (src gone afterward, dst holds src's former content). This
 * comes from the emulator calling Rust's std::fs::rename directly with no
 * existence check (deliberate POSIX-like semantics on every host OS). The
 * real FatFs MOS 2.3.3 ships explicitly checks for an existing
 * destination and returns FR_EXIST, refusing the rename - standard,
 * documented FatFs behaviour, expected to hold on real hardware. Until
 * that is confirmed on real hardware, check with mos_fopen first if
 * "refuse if the destination exists" safety must hold in both
 * environments. */
int mos_ren(const char *src, const char *dst);

int mos_mkdir(const char *path);

/* The size of the file at path (from ffs_stat), or -1 if it cannot be
 * found. */
long mos_fsize(const char *path);

/* Wait for a key; returns its ASCII code. */
int mos_getkey(void);

/* Load a file to address, at most maxsize bytes (0: all); returns the
 * status. */
int mos_load(const char *filename, void *address, unsigned int maxsize);

/* The real-time clock as text, "Sun, 27/09/2026 10:54:26", into buf (at
 * least 32 bytes); returns its length. */
int mos_getrtc(char *buf);

/* Run a MOS command line, as typed at the prompt; returns the status. */
int mos_oscli(const char *command);

/* The address of MOS's system variables block (MOS_SYSVAR below gives
 * it a structure). */
char *mos_sysvars(void);

/* Test support: exit the emulator with this status (port 0); returns
 * harmlessly on real hardware. */
void agon_emu_exit(int status);

/* ---- the rest of the MOS 2.3.3 API (mosapi.c, in libagon.s) ------------------------- */

/* Save nbytes from address to a new file (an existing one is an error);
 * returns the status. */
int mos_save(const char *filename, const void *address, unsigned int nbytes);

/* Change the current directory; returns the status. */
int mos_cd(const char *path);

/* List a directory on the screen, as the DIR command does; returns the
 * status. */
int mos_dir(const char *path);

/* Copy one file (not a directory); returns the status. */
int mos_copy(const char *src, const char *dst);

/* A byte from a file opened with mos_fopen (0-255); mos_feof tells
 * whether the file is used up. */
int mos_fgetc(int handle);

/* Write one byte to a file opened with mos_fopen. */
void mos_fputc(int handle, int c);

/* The FatFS FIL structure behind one of MOS's file handles, for the ffs_
 * calls below; it lives in MOS's memory. */
void *mos_getfil(int handle);

/* mos_editline's flags (MOS 2.2 and later for all but the first). */
#define MOS_EDIT_CLEAR      0x01    /* clear buf first */
#define MOS_EDIT_COMPLETE   0x02    /* Tab completes MOS commands and file names */
#define MOS_EDIT_NOHOTKEYS  0x04    /* the function-key hotkeys are off */
#define MOS_EDIT_NOHISTORY  0x08    /* no command history */

/* MOS's line editor: edit buf (size bytes, its contents shown first unless
 * MOS_EDIT_CLEAR) at the cursor until Return or Escape; returns the key
 * that ended it (13 or 27). */
int mos_editline(char *buf, unsigned int size, int flags);

/* The message for a status code, into buf (size bytes). */
void mos_getError(int code, char *buf, unsigned int size);

/* Set the real-time clock from six bytes: the year less 1980, month
 * (1-12), day (1-31), hour, minute, second. */
void mos_setrtc(const unsigned char *time);

/* The keyboard map: 16 bytes, one bit set for each key held down, by BBC
 * BASIC's key numbers (those INKEY(-n) takes): key n is bit (n-1)%8 of
 * byte (n-1)/8. MOS updates it as keys go down and up. */
unsigned char *mos_getkbmap(void);

/* UART1 by MOS's own calls (agon/uart.h has an easier interface). */
struct mos_uart {
    unsigned char baud[3];      /* the baud rate, low byte first */
    unsigned char data_bits;    /* 5 to 8 */
    unsigned char stop_bits;    /* 1 or 2 */
    unsigned char parity;       /* UART_PARITY_ in agon/uart.h */
    unsigned char flow;         /* non-zero: hardware flow control */
    unsigned char interrupts;   /* MOS's interrupt enables: 0 */
};
int mos_uopen(struct mos_uart *settings);   /* returns the status */
void mos_uclose(void);
int mos_ugetc(void);                        /* a byte (0-255), waiting for one; -1 if closed */
int mos_uputc(int c);                       /* 1 if sent, 0 if not */

/* I2C, as bus master. frequency: 1 for 57600, 2 for 115200, 3 for 230400.
 * A transfer is at most 32 bytes; it returns 0, or 1 (no answer), 2
 * (data not acknowledged), 4 (arbitration lost), 8 (bus error). */
void mos_i2c_open(int frequency);
void mos_i2c_close(void);
int mos_i2c_write(int address, int size, const unsigned char *buf);
int mos_i2c_read(int address, int size, unsigned char *buf);

/* ---- keyboard and interrupt handlers (handler.c, kbint.s, in libagon.s) --------------- */

/* A C function MOS calls for every key event, with the VDP's packet:
 * packet[0] the ASCII code (0 if none), [1] the modifiers (bit 0 Ctrl, 1
 * Shift, 2 left Alt, 3 right Alt, 4 Caps Lock, 5 Num Lock, 6 Scroll Lock,
 * 7 GUI), [2] the FabGL virtual key code, [3] 1 down, 0 up. It runs
 * inside MOS's serial interrupt, interrupts off: keep it short, and call
 * no MOS, stdio or VDU function; arithmetic is safe (the runtime's
 * working cells are saved around it). Ctrl-C keeps working, and the
 * handler goes when the program ends. 0 removes it. */
void mos_set_key_handler(void (*fn)(const unsigned char *packet));

/* A C function as the handler of interrupt vector (the eZ80's vector
 * table offset, an even number; enabling the source is up to the
 * program), with the same rules as a key handler. 0 for fn puts the
 * vector back as it was; every vector also goes back when the program
 * ends, however it ends. Four at once; returns 0, or -1 if all four are
 * taken. */
int mos_set_interrupt_handler(int vector, void (*fn)(void));

/* MOS's own calls, for handlers written in assembly: MOS calls a
 * keyboard vector with DE at the packet (addresslength 0: a 24-bit
 * address); an interrupt vector is entered by the hardware and must
 * save everything and end with EI and RETI.L. mos_setintvector returns
 * the vector's previous handler. */
void mos_setkbvector(void *handler, int addresslength);
void *mos_setintvector(int vector, void *handler);

/* ---- FatFS (mosapi.c, in libagon.s) --------------------------------------------------- */

/* The FatFS calls MOS 2.3.3 implements, with FatFS's own structures as MOS
 * was built (long file names on, exFAT off, no buffer in a FIL). They
 * return a FatFS result, FR_OK (0) on success. A FIL, DIR or FILINFO must
 * stay where it is while MOS uses it. Paths are as FatFS takes them,
 * relative to the current directory or from the root. */
#define FR_OK               0
#define FR_DISK_ERR         1
#define FR_INT_ERR          2
#define FR_NOT_READY        3
#define FR_NO_FILE          4
#define FR_NO_PATH          5
#define FR_INVALID_NAME     6
#define FR_DENIED           7
#define FR_EXIST            8
#define FR_INVALID_OBJECT   9
#define FR_WRITE_PROTECTED  10
#define FR_INVALID_DRIVE    11
#define FR_NOT_ENABLED      12
#define FR_NO_FILESYSTEM    13
#define FR_MKFS_ABORTED     14
#define FR_TIMEOUT          15
#define FR_LOCKED           16
#define FR_NOT_ENOUGH_CORE  17
#define FR_TOO_MANY_OPEN_FILES 18
#define FR_INVALID_PARAMETER 19

/* FILINFO's fattrib bits. */
#define AM_RDO  0x01    /* read only */
#define AM_HID  0x02    /* hidden */
#define AM_SYS  0x04    /* system */
#define AM_DIR  0x10    /* a directory */
#define AM_ARC  0x20    /* archive */

typedef struct {            /* FatFS's FFOBJID: the object's identity */
    void *fs;
    unsigned short id;
    unsigned char attr;
    unsigned char stat;
    unsigned long sclust;
    unsigned long objsize;
} FFOBJID;

typedef struct {            /* an open file */
    FFOBJID obj;
    unsigned char flag;
    unsigned char err;
    unsigned long fptr;     /* the read and write position */
    unsigned long clust;
    unsigned long sect;
    unsigned long dir_sect;
    unsigned char *dir_ptr;
} FIL;

typedef struct {            /* an open directory */
    FFOBJID obj;
    unsigned long dptr;
    unsigned long clust;
    unsigned long sect;
    unsigned char *dir;
    unsigned char fn[12];
    unsigned long blk_ofs;  /* (with long names; AgDev's DIR leaves it out) */
    const char *pat;
} DIR;

typedef struct {            /* a file's details */
    unsigned long fsize;    /* its size in bytes */
    unsigned short fdate;   /* modified: bits 15-9 year less 1980, 8-5 month, 4-0 day */
    unsigned short ftime;   /* bits 15-11 hour, 10-5 minute, 4-0 second / 2 */
    unsigned char fattrib;  /* AM_ bits */
    char altname[13];       /* the 8.3 name */
    char fname[256];        /* the long name ("" at the end of a directory) */
} FILINFO;

int ffs_fopen(FIL *fp, const char *path, int mode);  /* mode: FA_ bits, as mos_fopen */
int ffs_fclose(FIL *fp);
/* Read or write count bytes; *done (if done is not null) gets the count
 * transferred, short at the end of the file or when the disk is full. */
int ffs_fread(FIL *fp, void *buf, unsigned int count, unsigned int *done);
int ffs_fwrite(FIL *fp, const void *buf, unsigned int count, unsigned int *done);
int ffs_flseek(FIL *fp, unsigned long offset);       /* from the start */
int ffs_ftruncate(FIL *fp);                          /* the file ends at its position */
int ffs_feof(FIL *fp);                               /* 1 at the end, else 0 */
int ffs_stat(const char *path, FILINFO *info);
int ffs_dopen(DIR *dp, const char *path);
int ffs_dclose(DIR *dp);
/* The next entry into info; at the end, FR_OK with info->fname "". */
int ffs_dread(DIR *dp, FILINFO *info);
int ffs_getcwd(char *buf, unsigned int size);        /* the current directory's path */

/* ---- the system variables ------------------------------------------------------------- */

/* MOS's system variables, which MOS keeps current from the VDP's replies
 * and its interrupts: MOS_SYSVAR->cursor_x and so on. volatile: they
 * change under the program. Offsets as MOS 2.3.3 defines them (the
 * comment on each field), with no padding, as agonc lays out a struct.
 * Each use of MOS_SYSVAR is a MOS call (mos_sysvars); code reading
 * several fields can keep the pointer in a variable, as sysvar.c does. */
struct mos_sysvar {
    unsigned long time;             /* 00 centiseconds since start-up */
    unsigned char vdp_pflags;       /* 04 VDP reply flags */
    unsigned char keyascii;         /* 05 the last key's ASCII code, 0 if none */
    unsigned char keymods;          /* 06 its modifiers */
    unsigned char cursor_x;         /* 07 the text cursor */
    unsigned char cursor_y;         /* 08 */
    unsigned char scrchar;          /* 09 a character read from the screen */
    unsigned char scrpixel[3];      /* 0A a pixel read: R, B, G */
    unsigned char audio_channel;    /* 0D */
    unsigned char audio_success;    /* 0E 1 if the note was queued */
    unsigned short scr_width;       /* 0F the screen in pixels */
    unsigned short scr_height;      /* 11 */
    unsigned char scr_cols;         /* 13 the screen in characters */
    unsigned char scr_rows;         /* 14 */
    unsigned char scr_colours;      /* 15 */
    unsigned char scrpixel_index;   /* 16 the colour index of the pixel read */
    unsigned char vkeycode;         /* 17 the last key's FabGL virtual key code */
    unsigned char vkeydown;         /* 18 1 if it is down */
    unsigned char vkeycount;        /* 19 counts key events */
    unsigned char rtc[6];           /* 1A the clock, as mos_setrtc takes it */
    unsigned char spare[2];         /* 20 */
    unsigned short keydelay;        /* 22 keyboard repeat delay */
    unsigned short keyrate;         /* 24 and rate */
    unsigned char keyled;           /* 26 keyboard lights */
    unsigned char scr_mode;         /* 27 the screen mode */
    unsigned char rtc_enable;       /* 28 */
    unsigned short mouse_x;         /* 29 the mouse, when enabled */
    unsigned short mouse_y;         /* 2B */
    unsigned char mouse_buttons;    /* 2D */
    signed char mouse_wheel;        /* 2E */
    short mouse_xdelta;             /* 2F */
    short mouse_ydelta;             /* 31 */
    unsigned char reserved[4];      /* 33 */
    unsigned char gp;               /* 37 the general poll reply */
};

#define MOS_SYSVAR ((volatile struct mos_sysvar *)mos_sysvars())

/* The system variables one at a time, by AgDev's names, so that programs
 * written for AgDev build unchanged (sysvar.c). */
unsigned long getsysvar_time(void);
int getsysvar_vdp_pflags(void);
int getsysvar_keyascii(void);
int getsysvar_keymods(void);
int getsysvar_cursorX(void);
int getsysvar_cursorY(void);
int getsysvar_scrchar(void);
unsigned int getsysvar_scrpixel(void);      /* R, B, G bytes, R lowest */
int getsysvar_audioChannel(void);
int getsysvar_audioSuccess(void);
unsigned int getsysvar_scrwidth(void);
unsigned int getsysvar_scrheight(void);
int getsysvar_scrCols(void);
int getsysvar_scrRows(void);
int getsysvar_scrColours(void);
int getsysvar_scrpixelIndex(void);
int getsysvar_vkeycode(void);
int getsysvar_vkeydown(void);
int getsysvar_vkeycount(void);
volatile unsigned char *getsysvar_rtc(void);
unsigned int getsysvar_keydelay(void);
unsigned int getsysvar_keyrate(void);
int getsysvar_keyled(void);

#endif
