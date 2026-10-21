/* agon/timer.h - AgonDev's delay (v0.22), timed by the eZ80's timer 0
 * (hw.c, in libagon.s). Not part of C89. */

#ifndef _AGON_TIMER_H
#define _AGON_TIMER_H

void delay(unsigned int ms);    /* waits ms milliseconds; Ctrl-C stops it */

#endif
