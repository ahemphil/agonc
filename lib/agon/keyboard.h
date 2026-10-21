/* agon/keyboard.h - AgonDev's key event queue (v0.22): kbuf_init sets aside
 * room for buf_len key events and has MOS's keyboard handler keep each
 * one there; kbuf_poll_event takes the oldest. In libagon.s (handler.c),
 * on mos_set_key_handler, so Ctrl-C keeps working. A full queue drops the
 * newest events. int where AgonDev has uint8_t and bool. Not part of C89. */

#ifndef _AGON_KEYBOARD_H
#define _AGON_KEYBOARD_H

struct keyboard_event_t {
    unsigned char ascii;        /* the ASCII code, 0 if none */
    unsigned char kmod;         /* the modifier keys */
    unsigned char vkey;         /* the FabGL virtual key code */
    unsigned char isdown;       /* 1 down, 0 up */
};

void kbuf_init(int buf_len);                            /* 1 to 254 events */
int kbuf_poll_event(struct keyboard_event_t *e);        /* 1 and the oldest into *e, or 0 if none */
void kbuf_clear(void);
void kbuf_deinit(void);

#endif
