/* hw.c - the eZ80's own I/O, for AgonDev's <agon/gpio.h>,
 * <agon/joystick.h> and <agon/timer.h> (v0.22): ports read and written
 * directly, not through MOS; the GPIO pins of ports B, C and D, built on
 * them; the joysticks' switches; and a delay timed by timer 0. In
 * libagon.s.
 *
 * io_in and io_out take the port in BC: IN A,(C) and OUT (C),A put all of
 * BC on the eZ80's I/O address lines, and its internal registers sit at
 * 0x0000-0x00FF. addr is the first argument slot (ix+6), value the second
 * (ix+9); io_in's byte goes into r, the first local (ix-3).
 */

#include <agon/gpio.h>
#include <agon/joystick.h>
#include <agon/timer.h>

extern char __intflag;
void __interrupted(void);

int io_in(int addr)
{
    int r;

    asm("ld bc,(ix+6)\n"
        "in a,(bc)\n"
        "ld hl,0\n"
        "ld l,a\n"
        "ld (ix-3),hl");
    return r;
}

void io_out(int addr, int value)
{
    asm("ld bc,(ix+6)\n"
        "ld a,(ix+9)\n"
        "out (bc),a");
}

/* ---- GPIO ---- */

/* A port's register changed in some bits: set (on) or cleared. */
static void bits(int reg, int mask, int on)
{
    int v;

    v = io_in(reg);
    io_out(reg, on ? (v | mask) : (v & ~mask));
}

/* A pin's bit, or 0 for no pin (outside 0-7), which changes nothing. */
static int pin_bit(int pin)
{
    return pin >= 0 && pin <= 7 ? 1 << pin : 0;
}

void outputMode(int port, int pin)
{
    bits(port + 1, pin_bit(pin), 0);
}

void inputMode(int port, int pin)
{
    bits(port + 1, pin_bit(pin), 1);
}

void outputHigh(int port, int pin)
{
    bits(port, pin_bit(pin), 1);
}

void outputLow(int port, int pin)
{
    bits(port, pin_bit(pin), 0);
}

int input(int port, int pin)
{
    return (io_in(port) & pin_bit(pin)) != 0;
}

int inputPort(int port)
{
    return io_in(port);
}

void outputModePinID(int pin_id)
{
    bits((pin_id >> 8 & 255) + 1, pin_id & 255, 0);
}

void inputModePinID(int pin_id)
{
    bits((pin_id >> 8 & 255) + 1, pin_id & 255, 1);
}

void outputHighPinID(int pin_id)
{
    bits(pin_id >> 8 & 255, pin_id & 255, 1);
}

void outputLowPinID(int pin_id)
{
    bits(pin_id >> 8 & 255, pin_id & 255, 0);
}

int inputPinID(int pin_id)
{
    return (io_in(pin_id >> 8 & 255) & pin_id & 255) != 0;
}

/* ---- the joysticks ---- */

unsigned int getJoystickButtons(void)
{
    return (unsigned int)io_in(PC_DR) | (unsigned int)io_in(PD_DR) << 8;
}

void resetJoysticks(void)
{
    io_out(PC_DDR, 0xFF);
    io_out(PD_DDR, io_in(PD_DDR) | 0xF0);
}

/* ---- the delay ---- */

/* Timer 0 counts down from its reload value at the CPU clock / 16, so
 * 18,432,000 / 16 / 1000 = 1,152 counts a millisecond; AgonDev takes 19
 * off for the loop's own time. Each millisecond: the reload value, then
 * control 0x87 (enabled, one pass, stop at 0, start at once), then the
 * count read (low byte first, which latches the high) until it is 0. A
 * Ctrl-C is noticed between milliseconds. */
#define DELAY_COUNT (1152 - 19)

void delay(unsigned int ms)
{
    int n;

    io_out(TMR0_CTL, 0);
    while (ms > 0) {
        if (__intflag)
            __interrupted();
        io_out(TMR0_RR_H, DELAY_COUNT >> 8);
        io_out(TMR0_RR_L, DELAY_COUNT & 255);
        io_out(TMR0_CTL, 0x87);
        do {
            n = io_in(TMR0_DR_L);
            n = n | io_in(TMR0_DR_H) << 8;
        } while (n != 0);
        ms--;
    }
}
