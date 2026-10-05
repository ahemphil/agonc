/* mos.c - <agon/mos.h>: the MOS API wrappers that the C library itself
 * uses (files, the clock, the system variables), so they live in libc.s;
 * the rest of the MOS interface, the serial port and the VDU commands are
 * in libagon.s (uart.c, vdp.c and the other units in lib/agon).
 *
 * Where this sits: the C library (stdio.c's streams, remove and rename;
 * stdlib.c's system; time.c's clock) calls these, and they call MOS, the
 * Agon's operating system, which owns the SD card's file system, the clock
 * and the serial link to the VDP. The unit is compiled by agonc like any
 * other library unit; there is nothing to link against on the MOS side,
 * since a program reaches MOS only through the RST 08h instruction.
 *
 * ---- how a MOS call works ----
 * RST 08h is a one-byte call to a fixed address where MOS's API dispatcher
 * sits; A holds the function number and HL, DE, BC, C or E the arguments,
 * as MOS 2.3.3's API documentation lists them for each function. The .lis
 * suffix (rst.lis) is the form MOS documents for a caller in ADL mode
 * (24-bit addresses), as every agonc program is. Results come back in A
 * (a status, 0 for success, else a FatFs or MOS error number; or a handle
 * or a key), or in DE or IX for a count or an address.
 *
 * Each body is inline assembly under abi.md section 10: it loads the
 * RST 08h registers from the parameters, which are at (ix+6), (ix+9),
 * (ix+12) in order, and leaves the result in the function's first local,
 * r, which abi.md section 5 places at (ix-3) (the first scalar local is
 * nearest IX). A status or handle byte in A is zero-extended to a full
 * 24-bit value there, since C reads all three bytes of an int: `ld hl,0`
 * then `ld l,a`, never `ld h,0` / `ld l,a`, which would leave HL's top
 * byte stale (abi.md section 3, the canonical-value rule). MOS
 * preserves IX for every call used here except mos_sysvars, which returns
 * its pointer in IX and so saves and restores it. A byte parameter such
 * as a handle is read with a one-byte load, `ld a,(ix+6)`: the slot's low
 * byte is the value.
 *
 * Every call but mos_sysvars and agon_emu_exit starts at an interruption
 * point: a Ctrl-C pressed since the last one raises SIGINT there (exit.c's
 * __interrupted). Before the call, not after: then nothing
 * the call does (a handle opened, bytes written) is yet unrecorded by its
 * caller when SIGINT's default action closes the streams.
 *
 * The tests in tests/libc_c/t_mos.c define the behaviour. The
 * emulator-only file-system caveats are described in mos.h.
 */

#include <agon/mos.h>

/* __intflag is a byte in crt0.s that its keyboard handler sets, from
 * MOS's serial interrupt, when Ctrl-C goes down; __interrupted (exit.c)
 * clears it and raises SIGINT. POLL is the interruption point: one byte
 * test when no Ctrl-C is pending, which is the usual case. */
extern char __intflag;
void __interrupted(void);

#define POLL() if (__intflag) __interrupted()

/* ---- keys, loading and files by name ---- */

/* MOS 0x00 mos_getkey: waits for a key; A is its ASCII code. */
int mos_getkey(void)
{
    int r;

    POLL();
    asm("ld a,0x00\n"
        "rst.lis 08h\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    return r;
}

/* MOS 0x01 mos_load: HL the name, DE the address, BC the most bytes. */
int mos_load(const char *filename, void *address, unsigned int maxsize)
{
    int r;

    POLL();
    asm("ld hl,(ix+6)\n"
        "ld de,(ix+9)\n"
        "ld bc,(ix+12)\n"
        "ld a,0x01\n"
        "rst.lis 08h\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    return r;
}

/* MOS 0x05 mos_del, 0x06 mos_ren and 0x07 mos_mkdir: HL the path (and
 * DE the new name for a rename); A the status. */
int mos_del(const char *path)
{
    int r;

    POLL();
    asm("ld hl,(ix+6)\n"
        "ld a,0x05\n"
        "rst.lis 08h\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    return r;
}

int mos_ren(const char *src, const char *dst)
{
    int r;

    POLL();
    asm("ld hl,(ix+6)\n"
        "ld de,(ix+9)\n"
        "ld a,0x06\n"
        "rst.lis 08h\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    return r;
}

int mos_mkdir(const char *path)
{
    int r;

    POLL();
    asm("ld hl,(ix+6)\n"
        "ld a,0x07\n"
        "rst.lis 08h\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    return r;
}

/* ---- the clock and the system variables ---- */

/* MOS 0x12 mos_getrtc: the real-time clock as text into HL's buffer; A
 * is the text's length. */
int mos_getrtc(char *buf)
{
    int r;

    POLL();
    asm("ld hl,(ix+6)\n"
        "ld a,0x12\n"
        "rst.lis 08h\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    return r;
}

/* MOS 0x08 mos_sysvars: the system variables' address comes back in IX,
 * the frame pointer. So IX is pushed first, the result moved to HL
 * through the stack (push ix / pop hl), the frame pointer popped back,
 * and only then is (ix-3) the local r again. No POLL: reading the system
 * variables is not an interruption point (mos.h). */
char *mos_sysvars(void)
{
    char *r;

    asm("push ix\n"
        "ld a,0x08\n"
        "rst.lis 08h\n"
        "push ix\n"
        "pop hl\n"
        "pop ix\n"
        "ld (ix-3),hl");
    return r;
}

/* ---- files by MOS handle ---- */

/* MOS 0x0A mos_fopen: HL the name, C the FA_ mode bits; A the handle, 0
 * if the file could not be opened. MOS has eight handles in all and does
 * not close them when a program ends, so every open needs its close. */
int mos_fopen(const char *filename, int mode)
{
    int r;

    POLL();
    asm("ld hl,(ix+6)\n"
        "ld a,(ix+9)\n"
        "ld c,a\n"
        "ld a,0x0A\n"
        "rst.lis 08h\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    return r;
}

/* MOS 0x0B mos_fclose: C the handle (0 closes them all); see mos.h for
 * what A holds afterwards. */
int mos_fclose(int handle)
{
    int r;

    POLL();
    asm("ld a,(ix+6)\n"
        "ld c,a\n"
        "ld a,0x0B\n"
        "rst.lis 08h\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    return r;
}

/* MOS 0x0E mos_feof: C the handle; A 1 at the end of the file, else 0. */
int mos_feof(int handle)
{
    int r;

    POLL();
    asm("ld a,(ix+6)\n"
        "ld c,a\n"
        "ld a,0x0E\n"
        "rst.lis 08h\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    return r;
}

/* MOS 0x10 mos_oscli: HL a command line, run as if typed at the MOS
 * prompt; A the status. */
int mos_oscli(const char *command)
{
    int r;

    POLL();
    asm("ld hl,(ix+6)\n"
        "ld a,0x10\n"
        "rst.lis 08h\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    return r;
}

/* MOS 0x1A mos_fread and 0x1B mos_fwrite: C the handle, HL the buffer, DE
 * the count; DE comes back as the count transferred, a full 24-bit value
 * stored as it is. */
unsigned int mos_fread(int handle, void *buf, unsigned int count)
{
    unsigned int r;

    POLL();
    asm("ld a,(ix+6)\n"
        "ld c,a\n"
        "ld hl,(ix+9)\n"
        "ld de,(ix+12)\n"
        "ld a,0x1A\n"
        "rst.lis 08h\n"
        "ld (ix-3),de");
    return r;
}

unsigned int mos_fwrite(int handle, const void *buf, unsigned int count)
{
    unsigned int r;

    POLL();
    asm("ld a,(ix+6)\n"
        "ld c,a\n"
        "ld hl,(ix+9)\n"
        "ld de,(ix+12)\n"
        "ld a,0x1B\n"
        "rst.lis 08h\n"
        "ld (ix-3),de");
    return r;
}

/* MOS 0x1C mos_flseek: C the handle and the 32-bit offset from the start
 * in E:UHL. The offset arrives in two slots (abi.md section 4), its low
 * 24 bits at (ix+9) and its top byte at (ix+12), which is E:UHL as it
 * stands. A the status. */
int mos_flseek(int handle, unsigned long offset)
{
    int r;

    POLL();
    asm("ld a,(ix+6)\n"
        "ld c,a\n"
        "ld hl,(ix+9)\n"
        "ld e,(ix+12)\n"
        "ld a,0x1C\n"
        "rst.lis 08h\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    return r;
}

/* ffs_stat (fn 96h) fills a FatFs FILINFO - 278 bytes in MOS 2.3.3's
 * configuration, the 32-bit size first - here a local array, whose address
 * the second scalar local (ix-6) carries into the assembly (arrays are
 * laid out after all the scalars, abi.md section 5). MOS 0x96 takes HL
 * the FILINFO and DE the path; A the status. The size is put together
 * from its four bytes, low first; each is masked with 255 because char
 * is signed (abi.md section 2). */
long mos_fsize(const char *path)
{
    int r;
    char *info_p;
    char info[280];

    POLL();
    info_p = info;
    asm("ld hl,(ix-6)\n"
        "ld de,(ix+6)\n"
        "ld a,0x96\n"
        "rst.lis 08h\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    if (r != 0)
        return -1;
    return (info[0] & 255) + ((info[1] & 255) << 8) + ((long)(info[2] & 255) << 16)
           + ((long)(info[3] & 255) << 24);
}

/* ---- test support ---- */

/* A write to I/O port 0 ends fab-agon-emulator with that byte as its exit
 * status, which is how the test suites report a result. out0 does not
 * stop the CPU at once, so wait 65536 iterations; on
 * hardware port 0 is unused and this returns after the wait. The loop
 * counts in BC from 0: `dec bc` steps the whole 24-bit register, but
 * `ld a,b` / `or c` tests only its low 16 bits, so the loop ends after
 * 65536 steps, not 2^24. */
void agon_emu_exit(int status)
{
    asm("ld a,(ix+6)\n"
        "out0 (0x00),a\n"
        "ld bc,0\n"
        "@wait:\n"
        "dec bc\n"
        "ld a,b\n"
        "or c\n"
        "jr nz,@wait");
}
