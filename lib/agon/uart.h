/* agon/uart.h - the Agon's second serial port, UART1, through MOS
 * (mos_uopen, mos_uclose, mos_ugetc, mos_uputc; implemented in uart.c, in libagon.s).
 * For a serial printer (BBC BASIC's LLIST) or a link to another machine.
 * Not part of C89. */

#ifndef _AGON_UART_H
#define _AGON_UART_H

#define UART_PARITY_NONE 0
#define UART_PARITY_ODD 1
#define UART_PARITY_EVEN 3

/* Opens UART1 at baud (up to 24 bits: 9600, 115200, ...), with data bits
 * 5 to 8, stop bits 1 or 2, parity as above, and hardware flow control if
 * flow is non-zero; no interrupts. Returns MOS's status (0). */
int uart_open(long baud, int data_bits, int stop_bits, int parity, int flow);
void uart_close(void);

/* The next character received (0-255), waiting for one; -1 if the port is
 * closed. */
int uart_getc(void);

/* c sent: c (0-255), or -1 if the port is closed. */
int uart_putc(int c);

/* n bytes of p sent; the count sent (fewer if the port is closed). */
int uart_write(const char *p, int n);

#endif
