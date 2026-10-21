/* agon/gpio.h - AgonDev's GPIO (v0.22): the eZ80's I/O ports read and
 * written directly (not MOS calls), and the pins of ports B, C and D
 * (hw.c, in libagon.s). A port is its data register's address (PORTB is
 * PB_DR, <ez80f92.h>); its direction register follows it, a pin's bit 0
 * for an output and 1 for an input. A pin ID holds the port in its high
 * byte and the pins' bits in its low one (PB0 and the rest, or several of
 * one port's together). int where AgonDev has uint8_t and bool. Not part
 * of C89. */

#ifndef _AGON_GPIO_H
#define _AGON_GPIO_H

#include <ez80f92.h>

/* A byte from I/O port addr, and one to it: AgonDev's IO(addr) read and
 * written. */
int io_in(int addr);
void io_out(int addr, int value);

/* One pin (0-7; others do nothing) of a port, or the pins of a pin ID. */
void outputMode(int port, int pin);
void outputModePinID(int pin_id);
void inputMode(int port, int pin);
void inputModePinID(int pin_id);
void outputHigh(int port, int pin);
void outputHighPinID(int pin_id);
void outputLow(int port, int pin);
void outputLowPinID(int pin_id);
int input(int port, int pin);           /* 1 if the pin reads high, else 0 */
int inputPinID(int pin_id);             /* 1 if any of its pins reads high */
int inputPort(int port);                /* the whole port */

#define PORTB PB_DR
#define PORTC PC_DR
#define PORTD PD_DR
#define PB0 ((PB_DR << 8) | 0x01)
#define PB1 ((PB_DR << 8) | 0x02)
#define PB2 ((PB_DR << 8) | 0x04)
#define PB3 ((PB_DR << 8) | 0x08)
#define PB4 ((PB_DR << 8) | 0x10)
#define PB5 ((PB_DR << 8) | 0x20)
#define PB6 ((PB_DR << 8) | 0x40)
#define PB7 ((PB_DR << 8) | 0x80)
#define PC0 ((PC_DR << 8) | 0x01)
#define PC1 ((PC_DR << 8) | 0x02)
#define PC2 ((PC_DR << 8) | 0x04)
#define PC3 ((PC_DR << 8) | 0x08)
#define PC4 ((PC_DR << 8) | 0x10)
#define PC5 ((PC_DR << 8) | 0x20)
#define PC6 ((PC_DR << 8) | 0x40)
#define PC7 ((PC_DR << 8) | 0x80)
#define PD0 ((PD_DR << 8) | 0x01)
#define PD1 ((PD_DR << 8) | 0x02)
#define PD2 ((PD_DR << 8) | 0x04)
#define PD3 ((PD_DR << 8) | 0x08)
#define PD4 ((PD_DR << 8) | 0x10)
#define PD5 ((PD_DR << 8) | 0x20)
#define PD6 ((PD_DR << 8) | 0x40)
#define PD7 ((PD_DR << 8) | 0x80)

#endif
