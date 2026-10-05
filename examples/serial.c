/* serial.c - talking to another machine over the Agon's second serial
 * port, UART1, through <agon/uart.h>.
 *
 *     agonc -o serial.bin serial.c
 *     serial
 *
 * Connect UART1 (the Agon's GPIO pins, at 3.3 volts) to a PC's serial
 * adapter and open a terminal program on the PC at 9600 baud, 8 data
 * bits, no parity, 1 stop bit. The Agon sends a greeting, then echoes back
 * everything the PC types, in capitals, and shows it on its own screen.
 * The PC ends the session by sending Escape.
 *
 * uart_getc waits for each character, so the Agon does nothing else
 * meanwhile; a program that must also watch the keyboard would check
 * MOS's keyboard state between characters instead of waiting.
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <agon/uart.h>

#define ESCAPE 27

/* Send a C string. */
static void send(const char *s)
{
    uart_write(s, (int)strlen(s));
}

int main(void)
{
    int c;

    if (uart_open(9600L, 8, 1, UART_PARITY_NONE, 0) != 0) {
        printf("Could not open UART1\n");
        return 200;
    }
    printf("UART1 open at 9600 baud; waiting for the other end.\n");
    send("Hello from the Agon. Type away; Escape ends.\r\n");

    for (;;) {
        c = uart_getc();
        if (c < 0 || c == ESCAPE)       /* -1: the port closed */
            break;
        if (c == '\r') {                /* a terminal's Return */
            send("\r\n");
            putchar('\n');
            continue;
        }
        uart_putc(toupper(c));
        putchar(c);
    }

    send("\r\nGoodbye.\r\n");
    uart_close();
    printf("\nSession ended.\n");
    return 0;
}
