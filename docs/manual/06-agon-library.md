# The Agon library

Three headers give a C program the rest of the machine: `<agon/mos.h>` for
MOS (files, the clock, the keyboard, the system variables, the FatFS calls,
keyboard and interrupt handlers), `<agon/uart.h>` for the second serial
port, and `<agon/vdp.h>` for the VDP, which owns the screen, sound,
keyboard and mouse. None of them is part of C89, and all of them work in
both language modes. This chapter is a reference: one table per area, with
prose only where something needs explaining. What the VDP or MOS does with
a command is left to the Agon documentation, linked from each section; the
tables say what each function sends or calls.

The headers themselves are commented and remain the final word on argument
order and ranges: [`mos.h`](../../lib/agon/mos.h),
[`uart.h`](../../lib/agon/uart.h) and [`vdp.h`](../../lib/agon/vdp.h).

## How the library works

### Headers and linking

```c
#include <agon/mos.h>      /* MOS calls, FatFS, system variables, handlers */
#include <agon/uart.h>     /* UART1 */
#include <agon/vdp.h>      /* VDU commands */
```

The headers live in `/lib/agon`. Nothing needs to be named on the command
line to use them. The few MOS calls the C library itself relies on (opening,
reading, writing, seeking, testing and closing files by handle, deleting,
renaming, making directories, a file's size, loading, waiting for a key, the
clock, `mos_oscli` and the system variables' address) are in `libc.s`, which every program links. The
rest of the interface is in `/lib/libagon.s`, which the driver always passes
to `ld` last, marked "read only if needed": `ld` opens it, through its
index, only when the program still has an undefined function after
everything else, and then keeps only the functions the program reaches. A
program that uses none of it links as quickly as one that does not include
the headers at all, and one that calls `vdp_cls` gets `vdp_cls` and
`vdu`, not the rest of the library. `-nostdlib` leaves `libagon.s` out
along with the rest of the library (see [the driver
chapter](02-using-agonc.md)).

### How VDU bytes reach the VDP

The eZ80 has no access to video memory. Everything a program shows or
plays is a stream of bytes in the BBC Micro's VDU format, which MOS passes
over a serial link to the VDP, an ESP32 with its own firmware. In that
stream a control code (0 to 31, or 127) starts a command, and the VDP takes
a fixed number of argument bytes after it; any other byte is a character to
draw. VDU 23 opens everything the BBC Micro did not have: VDU 23 followed
by 32 to 255 redefines a character, 23 followed by 1 to 31 selects settings
and command groups (23, 27 is bitmaps and sprites), and VDU 23, 0, n are
the VDP's system commands, among them audio (23, 0, &85) and the buffered
command API (23, 0, &A0). The [VDP overview](https://agonplatform.github.io/agon-docs/VDP/)
and the [VDU command list](https://agonplatform.github.io/agon-docs/vdp/VDU-Commands/)
describe the protocol in full.

Every function in `<agon/vdp.h>` ends in one of two primitives: `vdu(c)`,
which sends one byte through MOS's RST 10h, and `vdu_n(p, n)`, which sends
`n` bytes from `p` through RST 18h (nothing if `n` is 0 or less). A command
function builds its bytes in a small array on the stack and sends them with
a single `vdu_n`, so it costs one MOS call however many bytes it has. A
few functions send a command in two or three pieces, or loop; the tables
say so where it matters.

The tables write the bytes the way the VDP documentation does: numbers
separated by commas, `&` for hexadecimal, and a trailing `;` on a value
sent as 16 bits. "VDU 23, 0, &89, 4, x; y;" is the seven bytes 23, 0,
0x89, 4, then x and y as two bytes each.

The C library's console output reaches the VDP by the same route. stdio
writes its console buffer at the end of every output call (unless `setvbuf`
has made the stream fully buffered), so `printf` and the VDU functions can
be mixed freely and the bytes arrive in program order. One difference: a
text stream turns `'\n'` into CR LF, while `vdu` and `vdu_n` send exactly
what they are given, so a bare 10 through `vdu` moves the cursor down
without returning it to the left margin.

### Values wider than a byte

Arguments are `int` (24 bits) unless a table shows another type. Where the
VDP takes a byte, the function sends the low 8 bits of its argument; where
it takes 16 bits, the low 16, low byte first. Values the VDP takes as 24
bits (sample lengths, buffer offsets, the mouse wheel's acceleration) are
`long` parameters, sent as three bytes, low first. The library does no
range checking: a value too large for its field is silently truncated, so
`vdp_audio_play_note(0, 200, ...)` sends a volume of 200, which the VDP
interprets as it sees fit.

Coordinates are the VDP's graphics units, 1280 by 1024 with the origin at
the bottom left, until `vdp_logical_scr_dims(0)` turns logical scaling off
(then they are pixels from the top left). A few commands work in pixels
whatever the setting: sprite positions, `vdp_draw_bitmap`
and `vdp_move_cursor_relative`; the tile engine works in tiles and pixel
offsets, and text positions are character columns and rows.

### Requests and replies

Some commands make the VDP answer: where is the text cursor, what colour is
this pixel, how big is the screen. The answer comes back up the serial
link, and MOS's serial interrupt stores it in MOS's system variables (see
[the system variables](#system-variables)) and sets a bit in the
`vdp_pflags` byte there. The library's request functions use that bit:

1. With interrupts off for a moment, clear the request's bit in
   `vdp_pflags` (MOS 2.3.3 has no call for this, so the library writes the
   byte itself).
2. Send the command.
3. If asked to wait, watch the bit until it is set again, giving up after
   about one second by MOS's centisecond clock.

Clearing before sending means a fast reply cannot be lost, and the timeout
means a VDP that never answers (one too old to know the command) cannot
hang the program. The "request" functions take a `wait` argument and leave
the answer in the system variables; the "return" functions always wait and
hand back the answer, or -1 if none came. Without waiting, a program can
send a request, do other work, and look at the flag later:

```c
volatile struct mos_sysvar *sv = MOS_SYSVAR;

vdp_request_pixel_colour(640, 512, 0);    /* send, do not wait */
/* ... other work ... */
if (sv->vdp_pflags & VDP_PFLAG_POINT)
    printf("index %d\n", sv->scrpixel_index);
```

The flags are defined in `<agon/vdp.h>`:

| Flag | Bit | Set by the reply to |
|---|---|---|
| `VDP_PFLAG_CURSOR` | 0x01 | the text cursor position |
| `VDP_PFLAG_SCRCHAR` | 0x02 | a character read from the screen |
| `VDP_PFLAG_POINT` | 0x04 | a pixel read, or a palette entry |
| `VDP_PFLAG_AUDIO` | 0x08 | an audio command's status |
| `VDP_PFLAG_MODE` | 0x10 | the screen dimensions and mode |
| `VDP_PFLAG_RTC` | 0x20 | the real-time clock |
| `VDP_PFLAG_MOUSE` | 0x40 | a mouse report |

Pixel reads and palette requests share `VDP_PFLAG_POINT` and the same
system variables, so only one of them should be outstanding at a time.

### Names

Where AgDev has a call, the function has AgDev's name and argument order
(AgDev's `agon/vdp_vdu.h` and `mos_api.h`), so most programs written for
AgDev's Agon headers build unchanged; AgDev's misspelt
`vdp_keyboard_cotrol` is kept beside `vdp_keyboard_control`, and the
system variables have AgDev's `getsysvar_` readers. The rest follow the
same pattern: `vdp_` and a description for a VDU command, `vdp_adv_` for
the buffered API and the 16-bit bitmap commands, `vdp_audio_` for sound,
`mos_` for a MOS call, `ffs_` for a FatFS call. Alongside them are short
names for everyday use: `vdu`, `vdu_n`, `vdp_mode`, `vdp_cls`,
`vdp_clg`, `vdp_colour`, `vdp_gcol`, `vdp_tab`, `vdp_cursor`,
`vdp_plot`, `vdp_move` and `vdp_draw`. Some of these share a name with
AgDev's; `vdp_colour`, `vdp_tab`, `vdp_cursor`, `vdp_move` and `vdp_draw`
have AgDev-style twins that send the same bytes (`vdp_set_text_colour`,
`vdp_cursor_tab`, `vdp_cursor_enable`, `vdp_move_to`, `vdp_line_to`).

A few functions deliberately differ from AgDev: `vdp_triangle`,
`vdp_circle` and `vdp_circle_radius` send PLOT codes that draw, where
AgDev's send the matching "move" codes, which draw nothing. [Porting from
AgDev](07-porting-from-agdev.md) lists the differences that matter when
moving a program across.

Commands added after the original VDP carry, in the tables, the Console8
VDP release that introduced them ("2.8.0"). An older VDP ignores a command
it does not know, and a request it does not know times out.

### Ctrl-C

The C library notes a Ctrl-C press from the keyboard interrupt and acts on
it at its next interruption point, raising `SIGINT` (whose default action
closes the streams and ends the program with status 130; see [the C
library](05-c-library.md) for `<signal.h>`). Every function in these three
headers that makes a MOS call or sends VDU bytes is an interruption point:
the check comes first, before anything is sent, so the program never stops
half way through a command. The exceptions are the system-variable reads
(`mos_sysvars`, `MOS_SYSVAR` and the `getsysvar_` functions),
`mos_getkbmap`, the four vector functions (`mos_setkbvector`,
`mos_setintvector`, `mos_set_key_handler`, `mos_set_interrupt_handler`)
and `agon_emu_exit`. `mos_getkey` cannot be interrupted while it waits;
a Ctrl-C pressed then is acted on at the next call.

The check is one byte test, inside `vdu` and `vdu_n` and at the top of
each MOS wrapper. The bulk transfers `vdp_load_bitmap` and
`vdp_audio_load_sample` send their data in 4096-byte pieces, each a
separate check. Compiled code itself contains no checks, so a loop that
calls nothing in the library cannot be stopped this way.

## The MOS interface: agon/mos.h

These are thin wrappers over MOS's RST 08h calls, one function per call;
the MOS column gives the function number for looking it up in the [MOS API
documentation](https://agonplatform.github.io/agon-docs/mos/API/). The
header covers every call MOS 2.3.3 provides. The calls MOS 3 added are
left out, so a program built with it runs on MOS 2.3.3 and MOS 3 alike.

### Status codes

Most calls return MOS's status: 0 for success, 1 to 19 for a FatFS result
(the `FR_` constants below), 20 to 26 for one of MOS's own errors.
`mos_getError` turns any of them into the message MOS prints, and the
[status code table](https://agonplatform.github.io/agon-docs/mos/API/#status-codes)
lists them.

### Files by MOS handle

MOS opens files by name and identifies them by a one-byte handle. It has
eight handles in all, and it does not close them when a program ends: a
handle a program forgets to close stays in use until something closes it
(`mos_fclose(0)` closes every file) or the machine is reset, so every successful `mos_fopen` needs its `mos_fclose`. (Streams opened
with `fopen` are closed by `exit`; raw handles are not.)

| Function | MOS | What it does |
|---|---|---|
| `mos_fopen(name, mode)` | 0x0A | opens a file; returns its handle, or 0 on failure |
| `mos_fclose(h)` | 0x0B | closes handle `h`; `h` 0 closes every file |
| `mos_fread(h, buf, n)` | 0x1A | reads up to `n` bytes; returns the count read |
| `mos_fwrite(h, buf, n)` | 0x1B | writes `n` bytes; returns the count written |
| `mos_flseek(h, off)` | 0x1C | moves to byte `off` (an `unsigned long`) from the start |
| `mos_feof(h)` | 0x0E | 1 at the end of the file, else 0 |
| `mos_fgetc(h)` | 0x0C | the next byte, 0 to 255 |
| `mos_fputc(h, c)` | 0x0D | writes one byte |
| `mos_getfil(h)` | 0x19 | the FatFS `FIL` behind the handle, in MOS's memory |
| `mos_fsize(path)` | 0x96 | the file's size as a `long`, or -1 if not found |
| `mos_load(name, addr, max)` | 0x01 | loads a file to `addr`, at most `max` bytes (0: all) |
| `mos_save(name, addr, n)` | 0x02 | saves `n` bytes to a new file |
| `mos_del(path)` | 0x05 | deletes a file |
| `mos_ren(old, new)` | 0x06 | renames a file |
| `mos_copy(src, dst)` | 0x11 | copies one file (not a directory) |
| `mos_mkdir(path)` | 0x07 | makes a directory |
| `mos_cd(path)` | 0x03 | changes the current directory |
| `mos_dir(path)` | 0x04 | lists a directory on the screen, as `DIR` does |

The calls without a stated result return the status, except `mos_fputc`,
which returns nothing. `mos_fread` and
`mos_fwrite` return an `unsigned int` count; `mos_fgetc` gives no sign of
the end of the file, so test `mos_feof` first. `mos_save` refuses to
overwrite an existing file. `mos_fsize` uses a 280-byte buffer on the
stack for MOS's file information.

`mos_fopen`'s mode is a combination of these bits, the same ones
`ffs_fopen` takes:

| Constant | Value | Meaning |
|---|---|---|
| `FA_READ` | 0x01 | open for reading |
| `FA_WRITE` | 0x02 | open for writing |
| `FA_CREATE_NEW` | 0x04 | create the file; fail if it exists |
| `FA_CREATE_ALWAYS` | 0x08 | create the file, emptying any existing one |
| `FA_OPEN_ALWAYS` | 0x10 | open the file, creating it if needed |
| `FA_OPEN_APPEND` | 0x30 | as `FA_OPEN_ALWAYS`, positioned at the end |

Use `FA_READ`, `FA_WRITE` or both, with at most one of the others; with
none of them the file must already exist.

`mos_fclose` on a specific handle returns that handle number, not a status
or a count of files still open, and it does so even for a handle that was
never open; this is how MOS 2.3.3 itself behaves. With handle 0 it returns
0.

The header records three behaviours seen only with fab-agon-emulator's
host-folder SD card, whose file layer is the emulator's own code rather
than MOS's FatFS: a short `mos_fread` fills the rest of the requested
count in `buf` with zeros; a handle opened with `FA_OPEN_ALWAYS` ignores
`mos_flseek` when writing and always appends; and `mos_ren` onto an
existing name replaces it instead of failing with `FR_EXIST`. None of these
is expected on real hardware. A program that must behave the same in both
places can open with plain `FA_WRITE` before seeking and writing, and
check for the destination with `mos_fopen` before renaming.

### Other MOS calls

| Function | MOS | What it does |
|---|---|---|
| `mos_getkey()` | 0x00 | waits for a key; returns its ASCII code |
| `mos_editline(buf, size, flags)` | 0x09 | MOS's line editor on `buf`; returns 13 or 27 |
| `mos_oscli(cmd)` | 0x10 | runs a command line as if typed at the prompt |
| `mos_getError(code, buf, size)` | 0x0F | the message for a status code, into `buf` |
| `mos_getrtc(buf)` | 0x12 | the clock as text; returns its length |
| `mos_setrtc(t)` | 0x13 | sets the clock from six bytes |
| `mos_sysvars()` | 0x08 | the address of the system variables |
| `mos_getkbmap()` | 0x1E | the address of the keyboard map |
| `agon_emu_exit(status)` | none | ends the emulator with `status` |

`mos_editline` edits the text already in `buf` (`size` bytes including the
terminating null) at the cursor until Return (13) or Escape (27) ends it.
Its `flags` combine `MOS_EDIT_CLEAR` (0x01, start with an empty buffer),
`MOS_EDIT_COMPLETE` (0x02, Tab completes commands and file names),
`MOS_EDIT_NOHOTKEYS` (0x04, the function-key hotkeys off) and
`MOS_EDIT_NOHISTORY` (0x08, no command history); all but the first need
MOS 2.2 or later.

`mos_oscli` returns MOS's status for the command. It runs MOS's built-in
commands and moslets, but not a program from `/bin`, which would be loaded
over the program that called it; the same is true of `system`.

`mos_getrtc` writes text of the form `Sun, 27/09/2026 10:54:26` into a
buffer of at least 32 bytes, and refreshes the clock bytes in the system
variables. `mos_setrtc` takes six bytes: the year less 1980, the month
(1 to 12), the day (1 to 31), the hour, the minute and the second.

`mos_getkbmap` returns a 16-byte map with one bit for each key held down,
numbered as BBC BASIC's `INKEY` with a negative argument numbers them: key
n is bit (n-1)%8 of byte (n-1)/8. MOS updates it as keys go down and up, so
a game can poll it without waiting for key events. The [keyboard
page](https://agonplatform.github.io/agon-docs/mos/Keyboard/) compares
the ways of reading the keyboard.

`agon_emu_exit` writes `status` to I/O port 0, which makes
fab-agon-emulator exit with that status; it exists for test programs. On
real hardware the port is unused and the function returns after a short
delay.

### UART1 and I2C by MOS's calls

`<agon/uart.h>` (below) is the easier way to use the serial port; these
are MOS's calls as they are.

| Function | MOS | What it does |
|---|---|---|
| `mos_uopen(settings)` | 0x15 | opens UART1 with a `struct mos_uart`; returns the status |
| `mos_uclose()` | 0x16 | closes UART1 |
| `mos_ugetc()` | 0x17 | waits for a byte (0 to 255); -1 if the port is closed |
| `mos_uputc(c)` | 0x18 | sends a byte; 1 if sent, 0 if not |
| `mos_i2c_open(freq)` | 0x1F | starts the I2C bus as master |
| `mos_i2c_close()` | 0x20 | stops it |
| `mos_i2c_write(addr, n, buf)` | 0x21 | writes `n` bytes to device `addr` |
| `mos_i2c_read(addr, n, buf)` | 0x22 | reads `n` bytes from device `addr` |

`struct mos_uart` holds the baud rate as three bytes, low first
(`baud[3]`), then one byte each for `data_bits` (5 to 8), `stop_bits` (1
or 2), `parity` (the `UART_PARITY_` values of `<agon/uart.h>`), `flow`
(non-zero for hardware flow control) and `interrupts` (leave it 0).

The I2C frequency is 1 for 57600, 2 for 115200 or 3 for 230400. A transfer
is at most 32 bytes, and the result is 0 for success, 1 if the device did
not answer, 2 if the data was not acknowledged, 4 if arbitration was lost
or 8 for a bus error.

### FatFS

MOS 2.3.3 passes a set of calls straight to the FatFS library it is built
on. They work with FatFS's own structures, declared in the header as MOS
was compiled (long file names on, exFAT off, no sector buffer inside a
`FIL`), and they return a FatFS result, `FR_OK` (0) on success. Paths are
relative to the current directory or start from the root. The [FatFS
section of the MOS API](https://agonplatform.github.io/agon-docs/mos/API/#fatfs-commands)
documents each call.

| Function | MOS | What it does |
|---|---|---|
| `ffs_fopen(fp, path, mode)` | 0x80 | opens a file into `*fp`; `mode` as `mos_fopen` |
| `ffs_fclose(fp)` | 0x81 | closes it |
| `ffs_fread(fp, buf, n, done)` | 0x82 | reads up to `n` bytes |
| `ffs_fwrite(fp, buf, n, done)` | 0x83 | writes `n` bytes |
| `ffs_flseek(fp, off)` | 0x84 | moves to byte `off` (an `unsigned long`) from the start |
| `ffs_ftruncate(fp)` | 0x85 | ends the file at the current position |
| `ffs_feof(fp)` | 0x8E | 1 at the end of the file, else 0 |
| `ffs_stat(path, info)` | 0x96 | a file's details into `*info` |
| `ffs_dopen(dp, path)` | 0x91 | opens a directory into `*dp` |
| `ffs_dclose(dp)` | 0x92 | closes it |
| `ffs_dread(dp, info)` | 0x93 | the next directory entry into `*info` |
| `ffs_getcwd(buf, size)` | 0x9E | the current directory's path into `buf` |

`ffs_fread` and `ffs_fwrite` store the number of bytes transferred in
`*done` when `done` is not null; it falls short at the end of the file or
when the disk is full. `ffs_dread` signals the end of the directory with
`FR_OK` and an empty `info->fname`. A `FIL` from `mos_getfil` lets these
calls work on a file opened with `mos_fopen`.

MOS keeps pointers into a `FIL`, `DIR` or `FILINFO` while it uses one, so
the structure must stay where it is: a `FIL` for as long as the file is
open, a `DIR` until `ffs_dclose`. Do not copy an open one, or let it go out
of scope (an automatic `FIL` in a function that returns with the file still
open, for instance). `FILINFO` is 278 bytes, a sizeable local on the
Agon's stack.

The structures, with the fields a program normally reads:

| Structure | Useful fields |
|---|---|
| `FIL` | `fptr`, the read and write position; `obj.objsize`, the file's size |
| `DIR` | none: pass it to the `ffs_d` calls |
| `FILINFO` | `fsize`, `fdate`, `ftime`, `fattrib`, `altname[13]`, `fname[256]` |

`fdate` packs the year less 1980 in bits 15 to 9, the month in bits 8 to 5
and the day in bits 4 to 0; `ftime` the hour in bits 15 to 11, the minute
in bits 10 to 5 and the second divided by two in bits 4 to 0. `altname` is
the 8.3 name and `fname` the long name. `fattrib` holds these bits:
`AM_RDO` (0x01, read only), `AM_HID` (0x02, hidden), `AM_SYS` (0x04,
system), `AM_DIR` (0x10, a directory) and `AM_ARC` (0x20, archive).
`DIR` includes the `blk_ofs` field that long file names add, which
AgDev's `DIR` leaves out.

The results are the standard FatFS ones:

| Constant | Value | Constant | Value |
|---|---|---|---|
| `FR_OK` | 0 | `FR_WRITE_PROTECTED` | 10 |
| `FR_DISK_ERR` | 1 | `FR_INVALID_DRIVE` | 11 |
| `FR_INT_ERR` | 2 | `FR_NOT_ENABLED` | 12 |
| `FR_NOT_READY` | 3 | `FR_NO_FILESYSTEM` | 13 |
| `FR_NO_FILE` | 4 | `FR_MKFS_ABORTED` | 14 |
| `FR_NO_PATH` | 5 | `FR_TIMEOUT` | 15 |
| `FR_INVALID_NAME` | 6 | `FR_LOCKED` | 16 |
| `FR_DENIED` | 7 | `FR_NOT_ENOUGH_CORE` | 17 |
| `FR_EXIST` | 8 | `FR_TOO_MANY_OPEN_FILES` | 18 |
| `FR_INVALID_OBJECT` | 9 | `FR_INVALID_PARAMETER` | 19 |

A directory listing:

```c
#include <stdio.h>
#include <agon/mos.h>

static DIR dir;             /* static: MOS keeps pointers into both */
static FILINFO info;

int main(void)
{
    if (ffs_dopen(&dir, ".") != FR_OK)
        return 1;
    while (ffs_dread(&dir, &info) == FR_OK && info.fname[0] != 0)
        printf("%-20s %8lu%s\n", info.fname, info.fsize,
               (info.fattrib & AM_DIR) ? " <dir>" : "");
    ffs_dclose(&dir);
    return 0;
}
```

### System variables

MOS keeps a block of system variables up to date from the VDP's replies
and from its own interrupts: the time since start-up, the last key, the
text cursor, the screen's size, the mouse. `MOS_SYSVAR` is a pointer to
that block as a `volatile struct mos_sysvar`, so `MOS_SYSVAR->cursor_x`
reads the current value each time. Each use of `MOS_SYSVAR` is a call to
`mos_sysvars`; code that reads several fields keeps the pointer:

```c
volatile struct mos_sysvar *sv = MOS_SYSVAR;

vdp_get_scr_dims(1);
printf("%d x %d pixels, %d colours\n",
       sv->scr_width, sv->scr_height, sv->scr_colours);
```

The fields, at MOS 2.3.3's offsets (agonc adds no padding to a struct, so
the layout matches byte for byte). The third column is the matching
AgDev-style reader, `getsysvar_` followed by the name shown, where there is
one; each reader returns the field as an `int`, `unsigned int` or
`unsigned long`.

| Offset | Field | `getsysvar_` | Meaning |
|---|---|---|---|
| 0x00 | `time` | `time` | centiseconds since start-up (`unsigned long`) |
| 0x04 | `vdp_pflags` | `vdp_pflags` | the VDP reply flags |
| 0x05 | `keyascii` | `keyascii` | the last key's ASCII code, 0 if none |
| 0x06 | `keymods` | `keymods` | its modifier keys |
| 0x07 | `cursor_x` | `cursorX` | the text cursor's column |
| 0x08 | `cursor_y` | `cursorY` | and row |
| 0x09 | `scrchar` | `scrchar` | a character read from the screen |
| 0x0A | `scrpixel[3]` | `scrpixel` | a pixel read: red, blue, green |
| 0x0D | `audio_channel` | `audioChannel` | the channel an audio reply refers to |
| 0x0E | `audio_success` | `audioSuccess` | an audio command's status |
| 0x0F | `scr_width` | `scrwidth` | the screen width in pixels |
| 0x11 | `scr_height` | `scrheight` | and height |
| 0x13 | `scr_cols` | `scrCols` | the screen width in characters |
| 0x14 | `scr_rows` | `scrRows` | and height |
| 0x15 | `scr_colours` | `scrColours` | the number of colours |
| 0x16 | `scrpixel_index` | `scrpixelIndex` | the colour index of a pixel read |
| 0x17 | `vkeycode` | `vkeycode` | the last key's FabGL virtual key code |
| 0x18 | `vkeydown` | `vkeydown` | 1 if that key is down |
| 0x19 | `vkeycount` | `vkeycount` | a count of key events |
| 0x1A | `rtc[6]` | `rtc` | the clock, in `mos_setrtc`'s layout |
| 0x22 | `keydelay` | `keydelay` | the keyboard repeat delay |
| 0x24 | `keyrate` | `keyrate` | the keyboard repeat rate |
| 0x26 | `keyled` | `keyled` | the keyboard lights |
| 0x27 | `scr_mode` | | the screen mode |
| 0x28 | `rtc_enable` | | |
| 0x29 | `mouse_x` | | the mouse position, when enabled |
| 0x2B | `mouse_y` | | |
| 0x2D | `mouse_buttons` | | |
| 0x2E | `mouse_wheel` | | (`signed char`) |
| 0x2F | `mouse_xdelta` | | the mouse's x movement (`short`) |
| 0x31 | `mouse_ydelta` | | |
| 0x37 | `gp` | | the general poll's echo |

The bytes at 0x20 (`spare`) and 0x33 (`reserved`) are unused. The
16-bit fields are little-endian, as agonc reads them. MOS stores a pixel
read's colour in the order red, blue, green; `getsysvar_scrpixel` returns
those three bytes as they are, red lowest, while `vdp_return_pixel_colour`
reorders them into 0xRRGGBB. `getsysvar_rtc` returns a pointer to the six
clock bytes. The [system variables
table](https://agonplatform.github.io/agon-docs/mos/API/#sysvars) in the
MOS documentation describes each field.

### Keyboard and interrupt handlers

A program can have a C function called on every key event, or on a
hardware interrupt.

| Function | What it does |
|---|---|
| `mos_set_key_handler(fn)` | calls `fn(packet)` on every key event; 0 removes it |
| `mos_set_interrupt_handler(vec, fn)` | makes `fn()` the handler of interrupt vector `vec` |
| `mos_setkbvector(handler, len)` | MOS 0x1D: an assembly keyboard routine |
| `mos_setintvector(vec, handler)` | MOS 0x14: an assembly interrupt routine |

A key handler receives the VDP's four-byte key packet:

| Byte | Contents |
|---|---|
| `packet[0]` | the ASCII code, 0 if the key has none |
| `packet[1]` | modifiers: bit 0 Ctrl, 1 Shift, 2 left Alt, 3 right Alt, 4 Caps Lock, 5 Num Lock, 6 Scroll Lock, 7 GUI |
| `packet[2]` | the FabGL virtual key code |
| `packet[3]` | 1 for a key going down, 0 for one coming up |

The [keyboard packet](https://agonplatform.github.io/agon-docs/vdp/System-Commands/#keyboard-packet-data)
is described with the VDP's system commands.

**What a handler may do.** A key handler runs inside MOS's serial
interrupt, and an interrupt handler inside the hardware interrupt, both
with interrupts off. While it runs, nothing else is serviced: the serial
link from the VDP, which carries key events and the replies to requests,
waits, and so do all other interrupts. So a handler should be short: note
what happened in a `volatile` variable and return, and let the main
program do the work. It must not call any MOS, stdio or VDU function
(they are not reentrant, and the main program may be in the middle of
one). Arithmetic of every kind is safe: the entry code saves the
registers, and also the fixed memory cells in which the runtime's
multiply, divide and shift helpers keep their working values, so a handler
that divides does not disturb a division the main program was in the
middle of.

The library's Ctrl-C check stays in place under a key handler (it runs
first), and the key handler is removed when the program ends, however it
ends. There is one key handler at a time; installing another replaces the
function, and 0 goes back to the Ctrl-C check alone.

```c
#include <stdio.h>
#include <agon/mos.h>

static volatile int presses;

static void count_key(const unsigned char *packet)
{
    if (packet[3] != 0)
        presses++;
}

int main(void)
{
    mos_set_key_handler(count_key);
    printf("Press keys; Escape ends\n");
    while (mos_getkey() != 27)
        ;
    mos_set_key_handler(0);
    printf("%d key presses\n", presses);
    return 0;
}
```

**Interrupt handlers.** `vec` is the offset of the vector in the eZ80's
interrupt vector table, an even number. The library installs an entry
routine there that saves every register, calls `fn`, and ends the
interrupt with EI and RETI.L; it does nothing at the device itself, so
enabling the interrupt source, and whatever the device needs to clear its
request, are the program's work. There are four slots, so up to four
vectors can have C handlers at once; `mos_set_interrupt_handler` returns
0, or -1 when all four are taken. Calling it again for a vector that
already has a C handler replaces the function without using another slot,
and passing 0 for `fn` puts back the handler the vector had before and
frees the slot. Every vector taken this way is put back when the program
ends, whether by returning from `main`, by `exit`, or by a signal.

**The raw calls.** `mos_setkbvector` and `mos_setintvector` are MOS's own
calls, for handlers written in assembly; they take none of the precautions
above. MOS calls a keyboard routine with DE pointing at the packet, and
`len` is 0 for a 24-bit address. There is only one keyboard vector, so a
routine installed with `mos_setkbvector` replaces the library's Ctrl-C
check; the library still clears the vector when the program ends. An
interrupt routine is entered by the hardware with nothing saved and must
save what it uses and end with EI and RETI.L. `mos_setintvector` returns
the vector's previous handler, which the program must put back itself
before it ends: unlike the slots above, nothing restores it.

## The serial port: agon/uart.h

UART0 is MOS's link to the VDP; UART1 is free for the program, for a
serial printer or a link to another machine. `<agon/uart.h>` drives it
through MOS's calls with plain arguments.

| Function | What it does |
|---|---|
| `uart_open(baud, data, stop, parity, flow)` | opens UART1; returns MOS's status (0) |
| `uart_close()` | closes it |
| `uart_getc()` | waits for a byte and returns it (0 to 255); -1 if the port is closed |
| `uart_putc(c)` | sends `c`; returns `c` (0 to 255), or -1 if the port is closed |
| `uart_write(p, n)` | sends `n` bytes from `p`; returns the count sent |

`baud` is a `long` (up to 24 bits: 9600, 115200 and so on), `data` the
data bits (5 to 8), `stop` the stop bits (1 or 2), `parity` one of
`UART_PARITY_NONE` (0), `UART_PARITY_ODD` (1) or `UART_PARITY_EVEN` (3),
and `flow` non-zero for hardware flow control. The port is opened with
MOS's interrupt enables off. `uart_write` makes one MOS call per byte
and stops at the first byte MOS does not take.

```c
#include <agon/uart.h>

uart_open(9600L, 8, 1, UART_PARITY_NONE, 0);
uart_write("HELLO\r\n", 7);
uart_close();
```

## VDU commands: agon/vdp.h

`<agon/vdp.h>` has a function for every command the VDP documentation
describes. The tables below follow the header's grouping. Functions
return nothing unless the table says otherwise.

### Raw bytes and the short names

| Function | Sends |
|---|---|
| `vdu(c)` | the byte `c` |
| `vdu_n(p, n)` | `n` bytes from `p` (a `const char *`); nothing if `n` is 0 or less |
| `vdp_mode(m)` | VDU 22, m: screen mode `m` |
| `vdp_cls()` | VDU 12: clear the text area |
| `vdp_clg()` | VDU 16: clear the graphics area |
| `vdp_colour(c)` | VDU 17, c: text colour; 128 + c for the background |
| `vdp_gcol(mode, c)` | VDU 18, mode, c: graphics colour and paint mode |
| `vdp_tab(x, y)` | VDU 31, x, y: text cursor to column `x`, row `y` |
| `vdp_cursor(on)` | VDU 23, 1, 0 or 1: hide or show the text cursor |
| `vdp_plot(k, x, y)` | VDU 25, k, x; y;: PLOT with BBC BASIC's code `k` |
| `vdp_move(x, y)` | PLOT 4: move to `x`, `y` |
| `vdp_draw(x, y)` | PLOT 5: a line from the last point to `x`, `y` |

`vdu` and `vdu_n` also serve for any command the library does not cover,
and for the variable-length part of the few commands whose functions send
only the start. The [screen modes](https://agonplatform.github.io/agon-docs/vdp/Screen-Modes/)
page lists what `vdp_mode` can select.

```c
#include <agon/vdp.h>

int main(void)
{
    vdp_cls();
    vdp_gcol(0, 3);             /* paint mode 0, colour 3 */
    vdp_move(100, 100);
    vdp_draw(1180, 924);        /* corner to corner, in graphics units */
    vdp_move(640, 512);
    vdp_circle_radius(200, 0);  /* centre 640, 512, radius 200 */
    return 0;
}
```

### VDU 1 to 31 and 127

AgDev's names, one per control code.

| Function | Sends |
|---|---|
| `vdp_send_to_printer(c)` | VDU 1, c: `c` to the printer (the VDP's USB serial port) |
| `vdp_enable_printer()` | VDU 2 |
| `vdp_disable_printer()` | VDU 3 |
| `vdp_write_at_text_cursor()` | VDU 4: text at the text cursor |
| `vdp_write_at_graphics_cursor()` | VDU 5: text at the graphics cursor |
| `vdp_enable_screen()` | VDU 6 |
| `vdp_bell()` | VDU 7 |
| `vdp_cursor_left()` | VDU 8 |
| `vdp_cursor_right()` | VDU 9 |
| `vdp_cursor_down()` | VDU 10 |
| `vdp_cursor_up()` | VDU 11 |
| `vdp_clear_screen()` | VDU 12, as `vdp_cls` |
| `vdp_carriage_return()` | VDU 13 |
| `vdp_page_mode_on()` | VDU 14: pause at each screenful |
| `vdp_page_mode_off()` | VDU 15 |
| `vdp_clear_graphics()` | VDU 16, as `vdp_clg` |
| `vdp_set_text_colour(c)` | VDU 17, as `vdp_colour` |
| `vdp_set_graphics_colour(mode, c)` | VDU 18, as `vdp_gcol` |
| `vdp_define_colour(l, p, r, g, b)` | VDU 19, l, p, r, g, b: logical colour `l` |
| `vdp_reset_graphics()` | VDU 20: default colours and modes |
| `vdp_disable_screen()` | VDU 21 |
| `vdp_set_graphics_viewport(l, b, r, t)` | VDU 24, l; b; r; t;: in graphics units |
| `vdp_reset_viewports()` | VDU 26 |
| `vdp_literal(c)` | VDU 27, c: `c` drawn as a character, even a control code |
| `vdp_set_text_viewport(l, b, r, t)` | VDU 28, l, b, r, t: in columns and rows |
| `vdp_graphics_origin(x, y)` | VDU 29, x; y; |
| `vdp_cursor_home()` | VDU 30 |
| `vdp_cursor_tab(x, y)` | VDU 31, as `vdp_tab` |
| `vdp_backspace()` | VDU 127 |

`vdp_define_colour` maps logical colour `l` to physical colour `p`, or,
when `p` is 255, to the colour given by `r`, `g` and `b` (0 to 255 each).
VDU 22, 23 and 25 are covered by the short names above and the sections
below.

### PLOT

`vdp_plot` sends any PLOT code; these send particular ones. The [PLOT
commands](https://agonplatform.github.io/agon-docs/vdp/PLOT-Commands/)
page lists the codes: the low three bits choose between moving and
drawing, the colour, and absolute or relative coordinates, and the rest
choose the shape.

| Function | Sends |
|---|---|
| `vdp_move_to(x, y)` | PLOT 4: move |
| `vdp_line_to(x, y)` | PLOT 5: a line from the last point |
| `vdp_point(x, y)` | PLOT 69: a point |
| `vdp_triangle(x, y)` | PLOT 85: a filled triangle with the last two points |
| `vdp_filled_rect(x, y)` | PLOT 101: a filled rectangle from the last point |
| `vdp_circle_radius(x, y)` | PLOT 145: a circle about the last point, radius `x`, `y` away |
| `vdp_circle(x, y)` | PLOT 149: a circle about the last point, through `x`, `y` |

`vdp_draw_bitmap` aside, PLOT codes &E8 to &EF draw the selected bitmap and
respect the viewports; send them with `vdp_plot`.

### VDU 23 settings

| Function | Sends |
|---|---|
| `vdp_redefine_character(c, b0, ..., b7)` | VDU 23, c, eight rows top first (c 32 to 255) |
| `vdp_cursor_enable(on)` | VDU 23, 1, as `vdp_cursor` |
| `vdp_cursor_control(n)` | VDU 23, 1, n: 0 hide, 1 show, 2 steady, 3 flashing |
| `vdp_set_dotted_line_pattern(b0, ..., b7)` | VDU 23, 6: the pattern, most significant bit first |
| `vdp_scroll_screen_extent(e, d, s)` | VDU 23, 7, e, d, s: scroll |
| `vdp_scroll_screen(d, s)` | VDU 23, 7, 1, d, s: scroll the whole screen |
| `vdp_cursor_behaviour(set, mask)` | VDU 23, 16, set, mask |
| `vdp_set_line_thickness(px)` | VDU 23, 23, px (2.6.0) |

`vdp_cursor_control`'s 2 and 3 need VDP 2.8.0. For scrolling, the extent
`e` is 0 for the text viewport, 1 the whole screen, 2 the graphics
viewport or 3 the active viewport; the direction `d` is 0 right, 1 left,
2 down or 3 up; and `s` is the distance in pixels, 0 meaning one
character. `vdp_cursor_behaviour` sets the cursor flags to (old AND
`mask`) XOR `set`.

### System commands

The VDP's system commands are VDU 23, 0 followed by a command byte; the
[system commands](https://agonplatform.github.io/agon-docs/vdp/System-Commands/)
page describes them. The "Sends" column gives what follows VDU 23, 0.

The text cursor's shape:

| Function | Sends |
|---|---|
| `vdp_set_cursor_start_line(n)` | &0A, n: first row; bits 5 and 6 the blink |
| `vdp_set_cursor_end_line(n)` | &0B, n: last row |
| `vdp_set_cursor_start_column(n)` | &8A, n (2.7.0) |
| `vdp_set_cursor_end_column(n)` | &8B, n (2.7.0) |
| `vdp_move_cursor_relative(x, y)` | &8C, x; y;: move by pixels (2.8.0) |

The blink bits of the start row are 0 for steady, 1 off, 2 fast and 3
slow.

### Requests

These wait as described under [requests and
replies](#requests-and-replies). The "request" forms take `wait`; the
"return" forms always wait and return -1 if no answer came within about a
second.

| Function | Sends | Answer |
|---|---|---|
| `vdp_general_poll(n)` | &80, n | `gp` = n; never waits |
| `vdp_request_text_cursor_position(wait)` | &82 | `cursor_x`, `cursor_y` |
| `vdp_return_text_cursor_position(&x, &y)` | &82 | 0, or -1 |
| `vdp_request_ascii_code_at_position(x, y, wait)` | &83, x; y; | `scrchar` |
| `vdp_return_ascii_code_at_position(x, y)` | &83, x; y; | the character |
| `vdp_request_ascii_code_at_graphics_position(x, y, wait)` | &93, x; y; | `scrchar` (2.8.0) |
| `vdp_return_ascii_code_at_graphics_position(x, y)` | &93, x; y; | the character |
| `vdp_request_pixel_colour(x, y, wait)` | &84, x; y; | `scrpixel`, `scrpixel_index` |
| `vdp_return_pixel_colour(x, y)` | &84, x; y; | `long` 0xRRGGBB |
| `vdp_request_palette_entry(n, wait)` | &94, n | `scrpixel`, `scrpixel_index` (2.4.0) |
| `vdp_return_palette_entry_colour(n)` | &94, n | `long` 0xRRGGBB |
| `vdp_return_palette_entry_index(n)` | &94, n | its colour number |
| `vdp_get_scr_dims(wait)` | &86 | the `scr_` fields |
| `vdp_request_rtc(wait)` | &87, 0 | `rtc` |
| `vdp_set_rtc(y, mo, d, h, mi, s)` | &87, 1, six bytes | sets the clock |

`vdp_return_text_cursor_position` stores the column and row through its
two `unsigned char *` arguments, leaving them alone on failure. The
character readers return 0 for a character the VDP does not recognise,
and -1 for no answer. A palette entry `n` is 0 to 63, or 128 to 131 for
the current text foreground and background and graphics foreground and
background. `vdp_set_rtc` takes the year less 1980, then the month, day,
hour, minute and second; it is not a request and does not wait.
`vdp_audio_status`, under [audio](#audio), is the other request.

### Keyboard and mouse

| Function | Sends |
|---|---|
| `vdp_set_keyboard_locale(n)` | &81, n |
| `vdp_keyboard_control(delay, rate, led)` | &88, delay; rate; led |
| `vdp_keyboard_cotrol(delay, rate, led)` | the same, AgDev's spelling |
| `vdp_control_keys(on)` | &98, 0 or 1: Ctrl+letter keys act on the VDP (2.6.0) |
| `vdp_request_key_state(vkey)` | &99, vkey: a fresh packet for one key (2.12.0) |
| `vdp_mouse_enable()` | &89, 0 |
| `vdp_mouse_disable()` | &89, 1 |
| `vdp_mouse_reset()` | &89, 2 |
| `vdp_mouse_set_cursor(n)` | &89, 3, n;: the pointer's shape |
| `vdp_mouse_set_position(x, y)` | &89, 4, x; y; |
| `vdp_mouse_sample_rate(r)` | &89, 6, r |
| `vdp_mouse_resolution(r)` | &89, 7, r |
| `vdp_mouse_scaling(s)` | &89, 8, s |
| `vdp_mouse_acceleration(a)` | &89, 9, a; |
| `vdp_mouse_wheel_accel(a)` | &89, 10, then `a` (a `long`) as 24 bits |

The keyboard locales are 0 UK, 1 US, 2 German, 3 Italian, 4 Spanish, 5
French, 6 Belgian, 7 Norwegian, 8 Japanese, 9 US International, 10 US
International Alternate, 11 Swiss German, 12 Swiss French, 13 Danish, 14
Swedish, 15 Portuguese, 16 Brazilian Portuguese and 17 Dvorak. The repeat
delay (250 to 1000) and rate (33 to 500) are in milliseconds, and `led`
sets the lights: bit 0 Scroll Lock, 1 Caps Lock, 2 Num Lock.

The mouse needs VDP 1.04 or later; MOS keeps its state in the system
variables (`mouse_x` and the rest). A cursor `n` is 0 to 18 for a built-in
shape, a bitmap's ID for one made with `vdp_mouse_cursor_from_bitmap`, or
65535 to hide the pointer. The sample rate is 10, 20, 40, 60, 80, 100 or
200 a second; the resolution 0 to 3 for 1, 2, 4 or 8 counts per
millimetre; the scaling 1 for 1:1 or 2 for 1:2. The [mouse
commands](https://agonplatform.github.io/agon-docs/vdp/System-Commands/#vdu-23-0-89-command-args-mouse-control)
give the details.

### Characters, viewports and the rest

| Function | Sends |
|---|---|
| `vdp_redefine_character_special(c, b0, ..., b7)` | &90, c, eight rows: any character 0 to 255 (2.3.0) |
| `vdp_define_character(c, data)` | the same from eight bytes at `data` |
| `vdp_reset_system_font()` | &91 |
| `vdp_map_char_to_bitmap(c, bitmap)` | &92, c, bitmap;: draw `c` as a bitmap (2.4.0) |
| `vdp_set_text_viewport_via_plot()` | &9C (2.8.0) |
| `vdp_set_graphics_viewport_via_plot()` | &9D (2.8.0) |
| `vdp_set_graphics_origin_via_plot()` | &9E (2.8.0) |
| `vdp_move_graphics_origin_and_viewport()` | &9F (2.8.0) |
| `vdp_set_affine_transform(flags, buffer)` | &96, flags, buffer; (2.9.0, experimental) |
| `vdp_page_mode_once()` | &9A: paged until output stops (2.14.0) |
| `vdp_print_buffer(buffer)` | &9B, buffer;: its bytes as characters (2.9.0) |
| `vdp_logical_scr_dims(on)` | &C0, 0 or 1: graphics units or pixels |
| `vdp_legacy_modes(on)` | &C1, 0 or 1: the modes of VDPs before 1.04 |
| `vdp_swap()` | &C3: swap screen buffers, or wait for a frame |
| `vdp_flush_drawing_commands()` | &CA (2.8.0) |
| `vdp_set_dash_pattern_length(n)` | &F2, n: 1 to 64, 0 the default (2.7.0) |
| `vdp_set_variable(id, value)` | &F8, id; value; (2.9.0) |
| `vdp_clear_variable(id)` | &F9, id; |
| `vdp_console_mode(on)` | &FE, 0 or 1: echo VDU bytes to the USB serial port |
| `vdp_terminal_mode()` | &FF: the VDP becomes a VT100 terminal |

The four "via plot" commands take their coordinates from the last
graphics points plotted. `vdp_set_affine_transform` applies the 3 by 3
matrix stored in a buffer to drawing (bit 0 of `flags` for bitmaps);
65535 for the buffer removes it. The [VDP
variables](https://agonplatform.github.io/agon-docs/vdp/VDP-Variables/)
page lists what `vdp_set_variable` can change.

### Bitmaps

Bitmap and sprite commands are VDU 23, 27 followed by a command byte; the
"Sends" column gives what follows VDU 23, 27. A bitmap with an 8-bit ID
`n` is stored in VDP buffer 64000 + `n`; the `vdp_adv_` commands take a
16-bit buffer ID instead and need VDP 2.2.0. The commands act on the
selected bitmap, so a program selects first and then loads or draws. The
[bitmaps and sprites](https://agonplatform.github.io/agon-docs/vdp/Bitmaps-API/)
page covers them.

| Function | Sends |
|---|---|
| `vdp_select_bitmap(n)` | 0, n |
| `vdp_load_bitmap(w, h, data)` | 1, w; h;, then w * h * 4 bytes of pixels |
| `vdp_load_bitmap_file(name, w, h)` | the same, the pixels read from a file |
| `vdp_capture_bitmap(n)` | 1, n, 0, 0;: the screen between the last two points (2.2.0) |
| `vdp_solid_bitmap(w, h, r, g, b, a)` | 2, w; h; r, g, b, a: one colour |
| `vdp_draw_bitmap(x, y)` | 3, x; y;: at pixel `x`, `y`, its top left corner |
| `vdp_adv_select_bitmap(buffer)` | &20, buffer; |
| `vdp_adv_bitmap_from_buffer(w, h, format)` | &21, w; h; format |
| `vdp_adv_capture_bitmap(buffer)` | &21, buffer; 0; |
| `vdp_mouse_cursor_from_bitmap(hx, hy)` | &40, hx, hy: the selected bitmap as a pointer |

`vdp_load_bitmap` takes `const unsigned long *data`: each pixel is four
bytes, red, green, blue and alpha in that order, rows from the top, so
read as an `unsigned long` a pixel is 0xAABBGGRR. The data goes in
4096-byte pieces. `vdp_load_bitmap_file` reads a file of pixels in that
format through a 256-byte buffer on the stack, sending each piece as it
arrives, so it needs no memory the size of the bitmap; it returns 0, or -1
if the file cannot be opened or ends early. If the file is short, what was
sent stays sent, and the VDP takes whatever bytes come next as the rest of
the pixel data, so a program should treat -1 here as leaving the VDP in
an unknown state.

`vdp_adv_bitmap_from_buffer` turns the selected buffer's contents into a
bitmap; `format` is 0 for RGBA8888, 1 for RGBA2222 or 2 for one bit per
pixel, drawn in the graphics colour. Loading through a buffer is quicker
than `vdp_load_bitmap` and allows the smaller formats. `vdp_draw_bitmap`
ignores the viewports; the PLOT codes &E8 to &EF are the better way to
draw. `vdp_mouse_cursor_from_bitmap` sets the hot spot; select the new
pointer with `vdp_mouse_set_cursor` and the bitmap's buffer ID.

### Sprites

Sprites, numbered 0 to 255, are positioned in pixels. The commands act on
the selected sprite: a typical sequence selects sprite `m`, clears it,
adds bitmaps as frames, shows it, and activates the sprites.

| Function | Sends |
|---|---|
| `vdp_select_sprite(n)` | 4, n |
| `vdp_clear_sprite()` | 5: no frames |
| `vdp_add_sprite_bitmap(n)` | 6, n: bitmap `n` as the next frame |
| `vdp_activate_sprites(n)` | 7, n: sprites 0 to n-1 are drawn |
| `vdp_next_sprite_frame()` | 8 |
| `vdp_prev_sprite_frame()` | 9 |
| `vdp_nth_sprite_frame(n)` | 10, n |
| `vdp_show_sprite()` | 11 |
| `vdp_hide_sprite()` | 12 |
| `vdp_move_sprite_to(x, y)` | 13, x; y; |
| `vdp_move_sprite_by(x, y)` | 14, x; y; |
| `vdp_refresh_sprites()` | 15: software sprites move on the screen now |
| `vdp_reset_sprites()` | 16: all bitmaps and sprites |
| `vdp_reset_sprites_only()` | 17: the sprites, keeping the bitmaps |
| `vdp_set_sprite_paint_mode(mode)` | 18, mode: a GCOL paint mode (2.6.0) |
| `vdp_set_sprite_hardware()` | 19 (2.12.0) |
| `vdp_set_sprite_software()` | 20 (2.12.0) |
| `vdp_replace_sprite_frame(n)` | 21, n;: bitmap `n` as the current frame (2.12.0) |
| `vdp_adv_add_sprite_bitmap(buffer)` | &26, buffer; |
| `vdp_adv_replace_sprite_frame(buffer)` | &35, buffer; |
| `vdp_create_sprite(s, first, frames)` | select, clear, then one add per frame |
| `vdp_adv_create_sprite(s, first, frames)` | the same with 16-bit buffer IDs |

`vdp_create_sprite` is a convenience with no VDU command of its own: it
makes sprite `s` from bitmaps `first` to `first + frames - 1` and leaves
it selected. A hardware sprite's frames must be RGBA8888 or RGBA2222.

### Audio

Audio commands are VDU 23, 0, &85, channel, command, and need VDP 1.04 or
later. The "Sends" column gives what follows the channel byte. Volumes are
0 to 127, frequencies in hertz, durations in milliseconds (65535: until
silenced), all 16 bits; sample lengths, offsets and durations sent as 24
bits are `long` parameters. The channel or sample number is sent as one
byte, so -1 and 255 are the same: a negative number names a sample (sample
-1 is stored in buffer 64256, -2 in 64257 and so on), and channel 255
means the whole sound system for the commands that say so. The [audio
API](https://agonplatform.github.io/agon-docs/vdp/Enhanced-Audio-API/)
describes the commands, the envelopes and the sample formats.

| Function | Sends |
|---|---|
| `vdp_audio_play_note(ch, vol, freq, dur)` | 0, vol, freq; dur; |
| `vdp_audio_status(ch)` | 1; returns the status bits, or -1 |
| `vdp_audio_set_volume(ch, vol)` | 2, vol (channel 255: overall, 2.5.0) |
| `vdp_audio_set_frequency(ch, freq)` | 3, freq; |
| `vdp_audio_set_waveform(ch, w)` | 4, w |
| `vdp_audio_set_sample(ch, buffer)` | 4, 8, buffer;: the sample in a buffer |
| `vdp_audio_load_sample(s, len, data)` | 5, 0, len (24 bits), then the data |
| `vdp_audio_clear_sample(s)` | 5, 1 |
| `vdp_audio_create_sample_from_buffer(ch, buffer, fmt)` | 5, 2, buffer; fmt |
| `vdp_audio_create_sample_from_buffer_rate(ch, buffer, fmt, rate)` | 5, 2, buffer; fmt + 8, rate; |
| `vdp_audio_set_sample_frequency(s, freq)` | 5, 3, freq; |
| `vdp_audio_set_buffer_frequency(ch, buffer, freq)` | 5, 4, buffer; freq; |
| `vdp_audio_set_sample_repeat_start(s, start)` | 5, 5, start (24 bits) |
| `vdp_audio_set_buffer_repeat_start(ch, buffer, start)` | 5, 6, buffer; start (24 bits) |
| `vdp_audio_set_sample_repeat_length(s, len)` | 5, 7, len (24 bits); -1: to the end |
| `vdp_audio_set_buffer_repeat_length(ch, buffer, len)` | 5, 8, buffer; len (24 bits) |
| `vdp_audio_volume_envelope_disable(ch)` | 6, 0 |
| `vdp_audio_volume_envelope_ADSR(ch, a, d, s, r)` | 6, 1, a; d; s, r; |
| `vdp_audio_volume_envelope_multiphase_ADSR(ch)` | 6, 2 only; the rest by `vdu_n` |
| `vdp_audio_volume_envelope_multiphase(ch, data, n)` | 6, 2, then `n` bytes (2.5.0) |
| `vdp_audio_frequency_envelope_disable(ch)` | 7, 0 |
| `vdp_audio_frequency_envelope_stepped(ch, np, ctl, len)` | 7, 1, np, ctl, len; then the phases by `vdu_n` |
| `vdp_audio_frequency_envelope(ch, ctl, len, phases, np)` | the whole stepped envelope |
| `vdp_audio_enable_channel(ch)` | 8 |
| `vdp_audio_disable_channel(ch)` | 9 |
| `vdp_audio_reset_channel(ch)` | 10 |
| `vdp_audio_sample_seek(ch, pos)` | 11, pos (24 bits) |
| `vdp_audio_sample_duration(ch, dur)` | 12, dur (24 bits) |
| `vdp_audio_sample_rate(ch, rate)` | 13, rate; (channel 255: overall) |
| `vdp_audio_set_waveform_parameter(ch, p, v)` | 14, p, v (16 bits if `p` has &80) |

Notes on the table:

- `vdp_audio_status` waits for the VDP's reply (flag
  `VDP_PFLAG_AUDIO`), as the other requests do, and returns the channel's
  state: bit values 1 active, 2 playing, 4 indefinite duration, 8 volume
  envelope, 16 frequency envelope, or 255 for a channel that is not
  enabled. Most other audio commands also make the VDP reply with a
  success flag, which MOS puts in `audio_success`; the library does not
  wait for those.
- Waveforms are 0 square, 1 triangle, 2 sawtooth, 3 sine, 4 noise and 5
  VIC noise; a negative waveform selects a sample by number.
- `vdp_audio_load_sample` takes a `long` length and a `const unsigned
  char *` to 8-bit signed samples at 16 kHz, sent in 4096-byte pieces. For
  long samples, a buffer (`vdp_adv_write_block_data`, then
  `vdp_audio_create_sample_from_buffer`) is the better route.
- A sample format is 0 for 8-bit signed or 1 for unsigned, plus 16 to make
  the sample tuneable; the `_rate` form adds 8, meaning a sample rate
  follows.
- The ADSR envelope's attack, decay and release are milliseconds; its
  sustain is a level, scaled by the note's volume.
- The multi-phase envelope's data is the attack's count and its phases,
  then the sustain's, then the release's, each phase a level byte and a
  16-bit duration, laid out exactly as the VDP takes them.
- A stepped frequency envelope has `np` phases; `phases` holds `2 * np`
  `int` values, an adjustment and a step count for each phase, each sent as
  16 bits. `ctl` combines 1 (repeat), 2 (cumulative) and 4 (restrict to 0
  to 65535); `len` is the step length.
- Up to 32 channels can be enabled; channels 0 to 2 are on at start-up.
- For `vdp_audio_set_waveform_parameter`, `p` is 0 for the duty cycle, 2
  for the volume, 3 for the frequency's low byte, or &83 for the whole
  frequency. A parameter with bit &80 set takes a 16-bit value.
- The sample sub-commands from 5, 3 on and the commands from 11 on need
  VDP 2.2.0.

### Buffers

The buffered command API, VDU 23, 0, &A0, buffer; command, stores blocks
of bytes on the VDP under 16-bit buffer IDs: data for bitmaps, samples and
fonts, sent once, or sequences of VDU commands that the VDP can replay on
one short call, with conditions and jumps, without the eZ80. Buffer 65535
means "this buffer" inside a stored sequence. The API needs VDP 1.04 or
later, and many commands later releases; the [buffered commands
API](https://agonplatform.github.io/agon-docs/vdp/Buffered-Commands-API/)
page describes every command and its options.

In this table the first argument of every function is the buffer ID (the
target buffer for the copying commands), left out of the argument column;
"Cmd" is the command number.

| Function | Cmd | Further arguments and effect |
|---|---|---|
| `vdp_adv_command` | any | `cmd, args, n`: any command with `n` argument bytes |
| `vdp_adv_write_block` | 0 | `len`: the next `len` bytes sent form a new block |
| `vdp_adv_write_block_data` | 0 | `len, data`: a whole block from `data` |
| `vdp_adv_call_buffer` | 1 | run the VDU commands in the buffer |
| `vdp_adv_clear_buffer` | 2 | empty it; 65535 clears every buffer |
| `vdp_adv_create` | 3 | `len`: a writeable buffer of `len` zero bytes |
| `vdp_adv_stream` | 4 | send the VDP's replies to the buffer |
| `vdp_adv_adjust` | 5 | `op, offset`: the start only |
| `vdp_adv_call_conditional` | 6 | `op, cbuf, coff`: the start only |
| `vdp_adv_jump_buffer` | 7 | jump to the buffer |
| `vdp_adv_jump_conditional` | 8 | `op, cbuf, coff`: the start only |
| `vdp_adv_jump_offset` | 9 | `offset` (a `long`) |
| `vdp_adv_jump_offset_block` | 9 | `offset, block` |
| `vdp_adv_jump_offset_conditional` | 10 | `offset`: the start only |
| `vdp_adv_jump_offset_block_conditional` | 10 | `offset, block`: the start only |
| `vdp_adv_call_offset` | 11 | `offset` |
| `vdp_adv_call_offset_block` | 11 | `offset, block` |
| `vdp_adv_call_offset_conditional` | 12 | `offset`: the start only |
| `vdp_adv_call_offset_block_conditional` | 12 | `offset, block`: the start only |
| `vdp_adv_copy_multiple` | 13 | `count, ...`: copy `count` buffers' blocks into this one |
| `vdp_adv_copy_blocks` | 13 | `ids, count`: the same, IDs from an array |
| `vdp_adv_consolidate` | 14 | join all blocks into one |
| `vdp_adv_split` | 15 | `size`: into blocks of `size` bytes |
| `vdp_adv_split_multiple` | 16 | `size, count, ...`: split, spread over `count` buffers |
| `vdp_adv_split_multiple_from` | 17 | `size, target`: split, one block per buffer from `target` |
| `vdp_adv_split_by_width` | 18 | `width, n`: a bitmap's rows into `n` blocks |
| `vdp_adv_split_by_width_multiple` | 19 | `width, count, ...` |
| `vdp_adv_split_by_width_multiple_from` | 20 | `width, n, target` |
| `vdp_adv_spread_multiple` | 21 | `count, ...`: blocks spread over `count` buffers |
| `vdp_adv_spread_multiple_from` | 22 | `target` |
| `vdp_adv_reverse_block_order` | 23 | reverse the order of the blocks |
| `vdp_adv_reverse_block_data` | 24 | `options, vsize, csize`: reverse each block's data |
| `vdp_adv_copy_multiple_by_reference` | 25 | `count, ...`: as 13, by reference (2.6.0) |
| `vdp_adv_copy_multiple_consolidate` | 26 | `count, ...`: as 13, then consolidated |
| `vdp_adv_compress_buffer` | 64 | `source`: compressed into this buffer |
| `vdp_adv_decompress_buffer` | 65 | `source`: decompressed into this buffer |
| `vdp_adv_set_callback` | 80 | `event`: run the buffer on an event (2.12.0) |
| `vdp_adv_remove_callback` | 81 | `event` |
| `vdp_adv_debug_info` | 128 | the buffer's details to the VDP's USB serial console |

Notes on the table:

- **Lengths and offsets.** Lengths and the `adjust` and condition offsets
  are 16 bits. The jump and call offsets of commands 9 to 12 are `long`
  and sent as 24 bits; the top bit of the third byte says a 16-bit block
  number follows, which the `_block` forms set, so the offset itself has
  23 bits.
- **"The start only".** Commands whose arguments vary with their options
  (adjust, and the conditions of 6, 8, 10 and 12) send their fixed part;
  the operands follow by `vdu_n`. `vdp_adv_command` sends the six-byte
  header and then any argument bytes the program builds, for these or any
  other command. The adjust operations are 0 NOT, 1 negate, 2 set, 3 add,
  4 add with carry, 5 AND, 6 OR, 7 XOR; the conditions 0 non-zero, 1
  zero, 2 equal, 3 not equal, 4 less, 5 greater, 6 less or equal, 7
  greater or equal, 8 AND, 9 OR; both take modifier bits as the VDP
  documentation describes.
- **Lists of IDs.** The variadic functions take `count` `int` buffer IDs
  after `count`, and send the list with its terminating 65535 added;
  `vdp_adv_copy_blocks` takes them from an array instead.
- **Reversing.** `vdp_adv_reverse_block_data`'s `options` combine 1
  (16-bit values), 2 (32-bit values), 3 (values of `vsize` bytes), 4
  (within chunks of `csize` bytes) and 8 (the block order too); `vsize`
  and `csize` are sent only when the options call for them.
- **Callbacks.** The events are 0 each frame, 1 a mode change, 2 a key, 3
  the mouse, 4 a palette change and 5 a pixel read; 2 to 5 need VDP
  2.15.0. For `vdp_adv_remove_callback`, 65535 as the event means every
  event, and 65535 as the buffer every buffer.
- `vdp_adv_stream` sends the VDP's replies to the buffer instead of to
  MOS; the VDP documentation advises against it, and VDP variables now do
  the job.

Storing a command sequence and replaying it:

```c
/* VDU 16 (clear the graphics area), then PLOT 69, 512; 512; (a point) */
static const char seq[] = { 16, 25, 69, 0, 2, 0, 2 };

vdp_adv_clear_buffer(1);
vdp_adv_write_block_data(1, sizeof seq, seq);
vdp_adv_call_buffer(1);         /* the VDP runs the seven bytes */
```

### Contexts, fonts, copper and tiles

The last four groups are each one system command whose next byte picks
the operation; the "Sends" column gives what follows VDU 23, 0.

**Graphics contexts** (&C8, VDP 2.8.0) save and restore the colours,
modes, font and cursors as a whole; save and restore push and pop the
current context's state on a stack. These functions send the commands as
documented, but they are a known issue: on the emulator and on a real
Agon, a text background colour set after `vdp_context_save` did not show
as documented. See the [context management
API](https://agonplatform.github.io/agon-docs/vdp/Context-Management-API/).

| Function | Sends |
|---|---|
| `vdp_context_select(n)` | &C8, 0, n |
| `vdp_context_delete(n)` | &C8, 1, n |
| `vdp_context_reset(flags)` | &C8, 2, flags: what to reset |
| `vdp_context_save()` | &C8, 3 |
| `vdp_context_restore()` | &C8, 4 |
| `vdp_context_save_copy(n)` | &C8, 5, n |
| `vdp_context_restore_all()` | &C8, 6 |
| `vdp_context_clear_stack()` | &C8, 7 |

**Fonts** (&95, VDP 2.8.0) are held in buffers; 65535 names the system
font. See the [font API](https://agonplatform.github.io/agon-docs/vdp/Font-API/).

| Function | Sends |
|---|---|
| `vdp_font_select(buffer, flags)` | &95, 0, buffer; flags |
| `vdp_font_create(buffer, w, h, ascent, flags)` | &95, 1, buffer; w, h, ascent, flags |
| `vdp_font_adjust(buffer, field, value)` | &95, 2, buffer; field, value; |
| `vdp_font_delete(buffer)` | &95, 4, buffer; |
| `vdp_font_copy(buffer)` | &95, 5, buffer;: the system font into a buffer |

**Copper palettes** (&C4, VDP 2.12.0) switch palettes as the screen is
drawn. The feature is enabled through VDP variable &310
(`vdp_set_variable`); the [copper
API](https://agonplatform.github.io/agon-docs/vdp/Copper-API/) explains
how.

| Function | Sends |
|---|---|
| `vdp_copper_create_palette(p)` | &C4, 0, p;: a copy of palette 0 |
| `vdp_copper_delete_palette(p)` | &C4, 1, p;: 65535 deletes them all |
| `vdp_copper_set_palette_entry(p, i, r, g, b)` | &C4, 2, p; i, r, g, b |
| `vdp_copper_set_signal_list(buffer)` | &C4, 3, buffer; |
| `vdp_copper_reset_signal_list()` | &C4, 4 |

The signal list in the buffer is pairs of 16-bit values: a number of rows
and the palette to use for them.

**The tile engine** (&C2, VDP 2.12.0) draws 8 by 8 RGBA2222 tiles from
banks, through a map of tile numbers and a layer that shows part of the
map. Some fields of the documented commands are fixed at 0 by this
interface. See the [tile engine](https://agonplatform.github.io/agon-docs/vdp/Tile-Engine/).

| Function | Sends |
|---|---|
| `vdp_tile_bank_init(bank)` | &C2, 0, bank, 0, 0, 0 |
| `vdp_tile_load(bank, id, pixels)` | &C2, 1, bank, id, then 64 bytes |
| `vdp_tile_draw(bank, id, x, y, xo, yo, attr)` | &C2, 6, bank, id, 0, x, y, xo, yo, attr |
| `vdp_tile_bank_free(bank)` | &C2, 7, bank |
| `vdp_tile_map_init(size)` | &C2, 16, 0, size, 0, 0 |
| `vdp_tile_map_set(x, y, id, attr)` | &C2, 17, 0, x, y, id, attr |
| `vdp_tile_map_free()` | &C2, 23, 0 |
| `vdp_tile_layer_init(size)` | &C2, 24, 0, size, 0, 0 |
| `vdp_tile_layer_scroll(x, y, xo, yo)` | &C2, 26, 0, x, y, xo, yo |
| `vdp_tile_layer_update()` | &C2, 28, 0 |
| `vdp_tile_layer_draw()` | &C2, 29, 0 |
| `vdp_tile_layer_update_draw()` | &C2, 30, 0 |

`vdp_tile_load` takes `const unsigned char *pixels`, 64 bytes for one
tile. `vdp_tile_draw` places a tile at tile position `x`, `y` plus pixel
offsets `xo`, `yo`. A map `size` is 0 to 8 for 32x32, 32x64, 32x128,
64x32, 64x64, 64x128, 128x32, 128x64 or 128x128 tiles; a layer `size` is
0 for 80x60, 1 80x30, 2 40x30 or 3 40x25 tiles, to suit 640x480,
640x240, 320x240 and 320x200 screen modes. In a map entry, `attr` bit 0
flips the tile horizontally, bit 1 vertically, and bits 2 and 3 choose the
bank; for `vdp_tile_draw`, bits 0 and 1 are the flips.
