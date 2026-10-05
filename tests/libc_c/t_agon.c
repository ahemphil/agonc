/* t_agon.c - the Agon extensions of M12 step 7: <agon/vdp.h>'s commands
 * run and return (what they draw is not checked: the emulator's screen is
 * not trustworthy, agon-emulator-output-flakiness), and <agon/uart.h>'s
 * port opens, sends, and reports -1 once closed. */

#include <agon/vdp.h>
#include <agon/uart.h>
#include "check.h"

int main(void)
{
    vdu(7);                             /* a bell: one byte */
    vdu_n("", 0);                       /* nothing, not "up to a delimiter" */
    vdu_n("\r\n", 2);
    vdp_cursor(0);
    vdp_colour(3);
    vdp_gcol(0, 2);
    vdp_move(100, 100);
    vdp_draw(300, 200);
    vdp_plot(69, 150, 150);             /* a point */
    vdp_tab(0, 0);
    vdp_colour(15);
    vdp_cursor(1);
    check(1, 1);                        /* came back from all of them */
    check(uart_open(9600L, 8, 1, UART_PARITY_NONE, 0), 0);
    check(uart_putc('A'), 'A');
    check(uart_putc(0xE9), 0xE9);
    check(uart_write("hello", 5), 5);
    uart_close();
    check(uart_putc('B'), -1);          /* closed */
    check(uart_write("x", 1), 0);
    return finish();
}
