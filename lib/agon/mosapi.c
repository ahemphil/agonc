/* mosapi.c - <agon/mos.h>: the MOS 2.3.3 API calls the C library does not
 * use itself (mos.c has those, in libc.s), the FatFS calls MOS 2.3.3
 * implements, and the keyboard map (sysvar.c has the system-variable
 * readers). In libagon.s, so only a program that calls one of them links
 * it: ld keeps just the sections a program reaches.
 *
 * mos.c's opening comment explains a MOS call (RST 08h, the function
 * number in A) and the zero-extension of a result byte; each function
 * here names its MOS function number in its `ld a,` line, and the
 * registers it loads are the ones MOS's API documentation gives for it.
 *
 * Each body loads the RST 08h registers from the parameters, at (ix+6),
 * (ix+9), (ix+12) and (ix+15) in order (a long takes two of these places:
 * its low 24 bits, then its top byte), and leaves a result in the first
 * local, r, at (ix-3); a second local is at (ix-6). MOS documents which
 * registers each call preserves, and not every call promises IX, so each
 * call saves IX around RST 08h itself. A byte result in A is
 * zero-extended, since C reads all three bytes of an int.
 *
 * MOS converts pointer arguments with MB only for a Z80-mode caller (MB
 * non-zero); an ADL program's 24-bit pointers pass through unchanged, so
 * anything in memory can be passed. Every call but the system-variable
 * reads (here mos_getkbmap) is an interruption point for Ctrl-C (mos.c).
 */

#include <agon/mos.h>

/* The Ctrl-C check before each call, as in mos.c. */
extern char __intflag;
void __interrupted(void);

#define POLL() if (__intflag) __interrupted()

/* ---- files and directories through MOS's own handles --------------------------- */

/* 0x02 mos_save: HL the name, DE the address, BC the byte count. */
int mos_save(const char *filename, const void *address, unsigned int nbytes)
{
    int r;

    POLL();
    asm("ld hl,(ix+6)\n"
        "ld de,(ix+9)\n"
        "ld bc,(ix+12)\n"
        "push ix\n"
        "ld a,0x02\n"
        "rst.lis 08h\n"
        "pop ix\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    return r;
}

/* 0x03 mos_cd, 0x04 mos_dir: HL the path. 0x11 mos_copy: HL the source,
 * DE the destination. */
int mos_cd(const char *path)
{
    int r;

    POLL();
    asm("ld hl,(ix+6)\n"
        "push ix\n"
        "ld a,0x03\n"
        "rst.lis 08h\n"
        "pop ix\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    return r;
}

int mos_dir(const char *path)
{
    int r;

    POLL();
    asm("ld hl,(ix+6)\n"
        "push ix\n"
        "ld a,0x04\n"
        "rst.lis 08h\n"
        "pop ix\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    return r;
}

int mos_copy(const char *src, const char *dst)
{
    int r;

    POLL();
    asm("ld hl,(ix+6)\n"
        "ld de,(ix+9)\n"
        "push ix\n"
        "ld a,0x11\n"
        "rst.lis 08h\n"
        "pop ix\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    return r;
}

/* 0x0C mos_fgetc: C the handle; A the byte. 0x0D mos_fputc: C the
 * handle, B the byte. The handle and the byte are each the low byte of
 * their slot, so a one-byte load straight into C or B is enough. */
int mos_fgetc(int handle)
{
    int r;

    POLL();
    asm("ld c,(ix+6)\n"
        "push ix\n"
        "ld a,0x0C\n"
        "rst.lis 08h\n"
        "pop ix\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    return r;
}

void mos_fputc(int handle, int c)
{
    POLL();
    asm("ld c,(ix+6)\n"
        "ld b,(ix+9)\n"
        "push ix\n"
        "ld a,0x0D\n"
        "rst.lis 08h\n"
        "pop ix");
}

/* The FIL that MOS keeps for one of its handles, for the ffs_ calls
 * (0x19 mos_getfil: C the handle; HL the address). */
void *mos_getfil(int handle)
{
    void *r;

    POLL();
    asm("ld c,(ix+6)\n"
        "push ix\n"
        "ld a,0x19\n"
        "rst.lis 08h\n"
        "pop ix\n"
        "ld (ix-3),hl");
    return r;
}

/* ---- the console, errors and the clock ------------------------------------------- */

/* 0x09 mos_editline: HL the buffer, BC its size, E the MOS_EDIT_ flags;
 * A the key that ended the edit. */
int mos_editline(char *buf, unsigned int size, int flags)
{
    int r;

    POLL();
    asm("ld hl,(ix+6)\n"
        "ld bc,(ix+9)\n"
        "ld e,(ix+12)\n"
        "push ix\n"
        "ld a,0x09\n"
        "rst.lis 08h\n"
        "pop ix\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    return r;
}

/* 0x0F mos_getError: E the status code, HL the buffer, BC its size. */
void mos_getError(int code, char *buf, unsigned int size)
{
    POLL();
    asm("ld e,(ix+6)\n"
        "ld hl,(ix+9)\n"
        "ld bc,(ix+12)\n"
        "push ix\n"
        "ld a,0x0F\n"
        "rst.lis 08h\n"
        "pop ix");
}

/* 0x13 mos_setrtc: HL the six time bytes (mos.h). */
void mos_setrtc(const unsigned char *time)
{
    POLL();
    asm("ld hl,(ix+6)\n"
        "push ix\n"
        "ld a,0x13\n"
        "rst.lis 08h\n"
        "pop ix");
}

/* ---- the keyboard map ------------------------------------------------------------- */

/* 0x1E mos_getkbmap: MOS returns the map's address in IX, so the frame
 * pointer is saved around it as in mos.c's mos_sysvars. */
unsigned char *mos_getkbmap(void)
{
    unsigned char *r;

    asm("push ix\n"
        "ld a,0x1E\n"
        "rst.lis 08h\n"
        "push ix\n"
        "pop hl\n"
        "pop ix\n"
        "ld (ix-3),hl");
    return r;
}

/* ---- UART1 by MOS's own names (uart.h has the friendlier ones) -------------------- */

/* 0x15 mos_uopen: the settings go to MOS in IX, so the pointer is moved
 * into IX through the stack (push hl / pop ix) after the frame pointer
 * is saved, and the frame pointer is back before r is stored. */
int mos_uopen(struct mos_uart *settings)
{
    int r;

    POLL();
    asm("ld hl,(ix+6)\n"
        "push ix\n"
        "push hl\n"
        "pop ix\n"
        "ld a,0x15\n"
        "rst.lis 08h\n"
        "pop ix\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    return r;
}

void mos_uclose(void)
{
    POLL();
    asm("push ix\n"
        "ld a,0x16\n"
        "rst.lis 08h\n"
        "pop ix");
}

/* 0x17 mos_ugetc. MOS sets carry when a byte arrived: the byte, else -1.
 * HL is zeroed first (ld does not touch the flags), so the carry still
 * decides the branch; `dec hl` from 0 gives -1 in all 24 bits. */
int mos_ugetc(void)
{
    int r;

    POLL();
    asm("push ix\n"
        "ld a,0x17\n"
        "rst.lis 08h\n"
        "pop ix\n"
        "ld hl,0\n"
        "jr nc,@none\n"
        "ld l,a\n"
        "jr @done\n"
        "@none:\n"
        "dec hl\n"
        "@done:\n"
        "ld (ix-3),hl");
    return r;
}

/* 0x18 mos_uputc: C the byte. Carry set when the byte was sent: 1, else
 * 0. */
int mos_uputc(int c)
{
    int r;

    POLL();
    asm("ld c,(ix+6)\n"
        "push ix\n"
        "ld a,0x18\n"
        "rst.lis 08h\n"
        "pop ix\n"
        "ld hl,0\n"
        "jr nc,@done\n"
        "inc hl\n"
        "@done:\n"
        "ld (ix-3),hl");
    return r;
}

/* ---- I2C ------------------------------------------------------------------------------ */

/* 0x1F mos_i2c_open: C the frequency code (mos.h); 0x20 closes. */
void mos_i2c_open(int frequency)
{
    POLL();
    asm("ld c,(ix+6)\n"
        "push ix\n"
        "ld a,0x1F\n"
        "rst.lis 08h\n"
        "pop ix");
}

void mos_i2c_close(void)
{
    POLL();
    asm("push ix\n"
        "ld a,0x20\n"
        "rst.lis 08h\n"
        "pop ix");
}

/* 0x21 mos_i2c_write and 0x22 mos_i2c_read: C the device address, B the
 * byte count, HL the buffer; A the result code. */
int mos_i2c_write(int address, int size, const unsigned char *buf)
{
    int r;

    POLL();
    asm("ld c,(ix+6)\n"
        "ld b,(ix+9)\n"
        "ld hl,(ix+12)\n"
        "push ix\n"
        "ld a,0x21\n"
        "rst.lis 08h\n"
        "pop ix\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    return r;
}

int mos_i2c_read(int address, int size, unsigned char *buf)
{
    int r;

    POLL();
    asm("ld c,(ix+6)\n"
        "ld b,(ix+9)\n"
        "ld hl,(ix+12)\n"
        "push ix\n"
        "ld a,0x22\n"
        "rst.lis 08h\n"
        "pop ix\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    return r;
}

/* ---- FatFS ---------------------------------------------------------------------------- */

/* MOS passes these on to FatFs's f_ functions (0x80 f_open, 0x81
 * f_close, ...), taking the C function's arguments in HL, DE and BC or C
 * - in order, except that ffs_stat takes the path in DE and the FILINFO
 * in HL - and returning FatFs's result in A. The structures are the
 * caller's, so they must stay put while MOS uses them (mos.h). */

/* 0x80: HL the FIL, DE the path, C the FA_ mode bits. */
int ffs_fopen(FIL *fp, const char *path, int mode)
{
    int r;

    POLL();
    asm("ld hl,(ix+6)\n"
        "ld de,(ix+9)\n"
        "ld c,(ix+12)\n"
        "push ix\n"
        "ld a,0x80\n"
        "rst.lis 08h\n"
        "pop ix\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    return r;
}

int ffs_fclose(FIL *fp)
{
    int r;

    POLL();
    asm("ld hl,(ix+6)\n"
        "push ix\n"
        "ld a,0x81\n"
        "rst.lis 08h\n"
        "pop ix\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    return r;
}

/* 0x82 and 0x83: HL the FIL, DE the buffer, BC the count. MOS returns
 * the count transferred in BC: into the second local, n at (ix-6), and
 * from there into *done if done is not null. */
int ffs_fread(FIL *fp, void *buf, unsigned int count, unsigned int *done)
{
    int r;
    unsigned int n;

    POLL();
    asm("ld hl,(ix+6)\n"
        "ld de,(ix+9)\n"
        "ld bc,(ix+12)\n"
        "push ix\n"
        "ld a,0x82\n"
        "rst.lis 08h\n"
        "pop ix\n"
        "ld (ix-6),bc\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    if (done != 0)
        *done = n;
    return r;
}

int ffs_fwrite(FIL *fp, const void *buf, unsigned int count, unsigned int *done)
{
    int r;
    unsigned int n;

    POLL();
    asm("ld hl,(ix+6)\n"
        "ld de,(ix+9)\n"
        "ld bc,(ix+12)\n"
        "push ix\n"
        "ld a,0x83\n"
        "rst.lis 08h\n"
        "pop ix\n"
        "ld (ix-6),bc\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    if (done != 0)
        *done = n;
    return r;
}

/* 0x84: HL the FIL. The offset arrives as its low 24 bits at (ix+9) and
 * top byte at (ix+12); MOS wants them in DE and C. */
int ffs_flseek(FIL *fp, unsigned long offset)
{
    int r;

    POLL();
    asm("ld hl,(ix+6)\n"
        "ld de,(ix+9)\n"
        "ld c,(ix+12)\n"
        "push ix\n"
        "ld a,0x84\n"
        "rst.lis 08h\n"
        "pop ix\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    return r;
}

int ffs_ftruncate(FIL *fp)
{
    int r;

    POLL();
    asm("ld hl,(ix+6)\n"
        "push ix\n"
        "ld a,0x85\n"
        "rst.lis 08h\n"
        "pop ix\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    return r;
}

int ffs_feof(FIL *fp)
{
    int r;

    POLL();
    asm("ld hl,(ix+6)\n"
        "push ix\n"
        "ld a,0x8E\n"
        "rst.lis 08h\n"
        "pop ix\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    return r;
}

/* 0x96: DE the path, HL the FILINFO (the one swapped pair). */
int ffs_stat(const char *path, FILINFO *info)
{
    int r;

    POLL();
    asm("ld de,(ix+6)\n"
        "ld hl,(ix+9)\n"
        "push ix\n"
        "ld a,0x96\n"
        "rst.lis 08h\n"
        "pop ix\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    return r;
}

int ffs_dopen(DIR *dp, const char *path)
{
    int r;

    POLL();
    asm("ld hl,(ix+6)\n"
        "ld de,(ix+9)\n"
        "push ix\n"
        "ld a,0x91\n"
        "rst.lis 08h\n"
        "pop ix\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    return r;
}

int ffs_dclose(DIR *dp)
{
    int r;

    POLL();
    asm("ld hl,(ix+6)\n"
        "push ix\n"
        "ld a,0x92\n"
        "rst.lis 08h\n"
        "pop ix\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    return r;
}

int ffs_dread(DIR *dp, FILINFO *info)
{
    int r;

    POLL();
    asm("ld hl,(ix+6)\n"
        "ld de,(ix+9)\n"
        "push ix\n"
        "ld a,0x93\n"
        "rst.lis 08h\n"
        "pop ix\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    return r;
}

/* 0x9E: HL the buffer, BC its size. */
int ffs_getcwd(char *buf, unsigned int size)
{
    int r;

    POLL();
    asm("ld hl,(ix+6)\n"
        "ld bc,(ix+9)\n"
        "push ix\n"
        "ld a,0x9E\n"
        "rst.lis 08h\n"
        "pop ix\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    return r;
}
