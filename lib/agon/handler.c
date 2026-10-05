/* handler.c - <agon/mos.h>'s keyboard and interrupt vectors: MOS's own
 * calls (mos_setkbvector, mos_setintvector), and C functions as handlers
 * through kbint.s's entry points. In libagon.s.
 *
 * A key handler is chained after the library's Ctrl-C check, and crt0's
 * __exit already takes MOS's keyboard vector away when the program ends.
 * An interrupt vector is put back as it was whenever the program ends,
 * however it ends: crt0's __exit calls __int_restore through a weak
 * reference, so a program that installs no handler pays nothing for it.
 *
 * Why the assembly entry points: MOS and the hardware call a vector
 * without the C calling convention (abi.md section 4). MOS enters a
 * keyboard vector with the packet's address in DE, not on the stack; the
 * hardware enters an interrupt vector with nothing saved and needs RETI
 * at the end. And a C function may use the runtime's multiply, divide
 * and shift helpers, which keep working values in fixed cells that the
 * interrupted program may be in the middle of using. kbint.s bridges
 * both: each entry saves the registers, calls the C function through
 * __handler_call (which also saves and restores those cells), and returns
 * the way its caller expects.
 *
 * ---- the slots ----
 * Four interrupt slots, each with a fixed entry in kbint.s (_int_entry0
 * to 3) that calls through its own _int_fn pointer. slot_vector records
 * which vector a slot serves, slot_old what that vector held before, so
 * removing a handler (or __int_restore at exit) puts the old one back.
 */

#include <agon/mos.h>

/* Read by kbint.s's entry points, where they are __kb_fn and __int_fn
 * (a C name gains a leading underscore in assembly, abi.md section 7).
 * _int_fn[s] is at __int_fn + 3*s, a pointer being three bytes. */
void (*_kb_fn)(const unsigned char *packet);
void (*_int_fn[4])(void);

void _kb_entry(void);                   /* kbint.s */
void _int_entry0(void);
void _int_entry1(void);
void _int_entry2(void);
void _int_entry3(void);
void __kb_on(void);                     /* crt0.s: the Ctrl-C vector alone */

static void (*entry[4])(void) = { _int_entry0, _int_entry1, _int_entry2, _int_entry3 };
/* Plus 1 because vector offset 0 is a real vector, so 0 can mean free. */
static int slot_vector[4];              /* the vector a slot serves, plus 1; 0 free */
static void *slot_old[4];               /* what the vector held before */

/* MOS's call 0x1D: handler (an assembly routine: MOS calls it from its
 * UART0 interrupt with DE at the key packet), or 0 for none. HL the
 * handler, C the address length (0: a 24-bit address). Unlike the other
 * MOS wrappers, neither this nor mos_setintvector starts with a Ctrl-C
 * check. */
void mos_setkbvector(void *handler, int addresslength)
{
    asm("ld hl,(ix+6)\n"
        "ld c,(ix+9)\n"
        "push ix\n"
        "ld a,0x1D\n"
        "rst.lis 08h\n"
        "pop ix");
}

/* MOS's call 0x14: vector's handler (an assembly routine the hardware
 * enters); returns what the vector held. E the vector, HL the handler;
 * HL comes back as the previous handler. */
void *mos_setintvector(int vector, void *handler)
{
    void *r;

    asm("ld e,(ix+6)\n"
        "ld hl,(ix+9)\n"
        "push ix\n"
        "ld a,0x14\n"
        "rst.lis 08h\n"
        "pop ix\n"
        "ld (ix-3),hl");
    return r;
}

/* The pointer is stored before the vector points at _kb_entry, so the
 * entry never calls a stale or null _kb_fn. Removing the handler
 * reinstalls crt0's Ctrl-C handler directly rather than leaving
 * _kb_entry in place. */
void mos_set_key_handler(void (*fn)(const unsigned char *packet))
{
    if (fn == 0) {
        __kb_on();                      /* back to the Ctrl-C check alone */
        return;
    }
    _kb_fn = fn;
    mos_setkbvector((void *)_kb_entry, 0);
}

/* First look for a slot already serving vector: replace its function, or
 * with fn 0 give the vector back its old handler and free the slot.
 * Otherwise take a free slot, recording the vector's old handler as
 * mos_setintvector hands it back. As with the key handler, _int_fn[s] is
 * set before the vector can reach the slot's entry. */
int mos_set_interrupt_handler(int vector, void (*fn)(void))
{
    int s;

    for (s = 0; s < 4; s++) {
        if (slot_vector[s] == vector + 1) {
            if (fn != 0) {
                _int_fn[s] = fn;        /* the slot's entry is already in place */
                return 0;
            }
            mos_setintvector(vector, slot_old[s]);
            slot_vector[s] = 0;
            return 0;
        }
    }
    if (fn == 0)
        return 0;                       /* nothing installed: nothing to undo */
    for (s = 0; s < 4; s++) {
        if (slot_vector[s] == 0) {
            _int_fn[s] = fn;
            slot_old[s] = mos_setintvector(vector, (void *)entry[s]);
            slot_vector[s] = vector + 1;
            return 0;
        }
    }
    return -1;
}

/* crt0's __exit, through a weak reference: every vector back as it was. */
void __int_restore(void)
{
    int s;

    for (s = 0; s < 4; s++) {
        if (slot_vector[s] != 0) {
            mos_setintvector(slot_vector[s] - 1, slot_old[s]);
            slot_vector[s] = 0;
        }
    }
}
