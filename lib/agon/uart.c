/* uart.c - <agon/uart.h>: the Agon's second serial port, UART1, through
 * MOS (mos_uopen, mos_uclose, mos_ugetc, mos_uputc). In libagon.s. The
 * bodies follow mos.c's conventions: parameters at (ix+6) on, the result
 * in the first local at (ix-3).
 *
 * UART0 is MOS's link to the VDP; UART1 is free for the program. These
 * are the same MOS calls as mosapi.c's mos_uopen, mos_uclose, mos_ugetc
 * and mos_uputc, with plain arguments in place of struct mos_uart and
 * with uart_putc answering c or -1, in the manner of putc.
 */

#include <agon/uart.h>

extern char __intflag;
void __interrupted(void);

/* Each call is an interruption point for Ctrl-C (mos.c). */
#define POLL() if (__intflag) __interrupted()

/* mos_uopen takes the settings as a struct at IX (MOS-API.md 0x15). cfg
 * is that struct (struct mos_uart's layout in mos.h): the baud rate as
 * three bytes, low first, then one byte each for the data bits, stop
 * bits, parity, flow control and interrupt enables. Its address reaches
 * the assembly through p, the second scalar local at (ix-6); there it is
 * moved into IX through the stack, with the frame pointer saved first
 * and restored before r is stored. */
int uart_open(long baud, int data_bits, int stop_bits, int parity, int flow)
{
    int r;
    char *p;
    char cfg[8];

    POLL();
    cfg[0] = (char)(baud & 255);
    cfg[1] = (char)(baud >> 8 & 255);
    cfg[2] = (char)(baud >> 16 & 255);
    cfg[3] = data_bits;
    cfg[4] = stop_bits;
    cfg[5] = parity;
    cfg[6] = flow != 0;
    cfg[7] = 0;                         /* no interrupts */
    p = cfg;
    asm("ld hl,(ix-6)\n"
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

/* MOS 0x16 mos_uclose. */
void uart_close(void)
{
    POLL();
    asm("ld a,0x16\n"
        "rst.lis 08h");
}

/* MOS 0x17 mos_ugetc answers with carry set on success, clear if the
 * port is closed. HL is zeroed before the branch (ld leaves the flags
 * alone): then `ld l,a` zero-extends the byte, or `dec hl` makes -1. */
int uart_getc(void)
{
    int r;

    POLL();
    asm("ld a,0x17\n"
        "rst.lis 08h\n"
        "ld hl,0\n"
        "jr nc,@closed\n"
        "ld l,a\n"
        "jr @done\n"
        "@closed:\n"
        "dec hl\n"
        "@done:\n"
        "ld (ix-3),hl");
    return r;
}

/* MOS 0x18 mos_uputc: C the byte; carry set if it was sent. The result
 * is c's low byte, reloaded from its slot, or -1. */
int uart_putc(int c)
{
    int r;

    POLL();
    asm("ld c,(ix+6)\n"
        "ld a,0x18\n"
        "rst.lis 08h\n"
        "ld hl,0\n"
        "jr nc,@closed\n"
        "ld l,(ix+6)\n"
        "jr @done\n"
        "@closed:\n"
        "dec hl\n"
        "@done:\n"
        "ld (ix-3),hl");
    return r;
}

/* One MOS call per byte; stops at the first byte MOS does not take. */
int uart_write(const char *p, int n)
{
    int i;

    for (i = 0; i < n; i++)
        if (uart_putc(p[i]) < 0)
            break;
    return i;
}

