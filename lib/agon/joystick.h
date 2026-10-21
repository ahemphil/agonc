/* agon/joystick.h - AgonDev's joystick port (v0.22): the two joysticks'
 * switches on GPIO ports C and D (hw.c, in libagon.s). Not part of C89. */

#ifndef _AGON_JOYSTICK_H
#define _AGON_JOYSTICK_H

/* Port C's pins in the low byte, port D's in the high: a switch pressed
 * reads 0, so each macro is true while its switch is pressed. */
unsigned int getJoystickButtons(void);
/* Port C's pins, and port D's top four, as inputs (port D's others kept). */
void resetJoysticks(void);

#define joystick2_UP(buttons)         (!((buttons) & (1U << (0))))
#define joystick1_UP(buttons)         (!((buttons) & (1U << (1))))
#define joystick2_DOWN(buttons)       (!((buttons) & (1U << (2))))
#define joystick1_DOWN(buttons)       (!((buttons) & (1U << (3))))
#define joystick2_LEFT(buttons)       (!((buttons) & (1U << (4))))
#define joystick1_LEFT(buttons)       (!((buttons) & (1U << (5))))
#define joystick2_RIGHT(buttons)      (!((buttons) & (1U << (6))))
#define joystick1_RIGHT(buttons)      (!((buttons) & (1U << (7))))
#define joystick2_BUTTON1(buttons)    (!((buttons) & (1U << (12))))
#define joystick1_BUTTON1(buttons)    (!((buttons) & (1U << (13))))
#define joystick2_BUTTON2(buttons)    (!((buttons) & (1U << (14))))
#define joystick1_BUTTON2(buttons)    (!((buttons) & (1U << (15))))

#endif
