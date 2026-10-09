; rt.s test: __imul. Exits via port 0 with the number of failed cases
; (0 = all passed). No crt0 needed - a plain MOS program calling the
; helper directly on MOS's own stack.

        assume  adl=1
        org     0x040000

        jp      start
        align   64
        db      "MOS", 0, 1

__bss_base:      equ     0x0AFF00
__ishru_buf:     equ     __bss_base+14
__ishrs_buf:     equ     __bss_base+17
__sext8_buf:     equ     __bss_base+20
__sext16_buf:    equ     __bss_base+23

        include "lib\rt\rt.s"

start:
        push    ix
        ld      ix, 0
        add     ix, sp
        ld      hl, 0
        ld      (fail_count), hl

        ; 0 * 0 = 0
        ld      de, 0
        ld      hl, 0
        call    __imul
        ld      de, 0
        call    @check

        ; 3 * 5 = 15
        ld      de, 3
        ld      hl, 5
        call    __imul
        ld      de, 15
        call    @check

        ; -3 * 5 = -15
        ld      de, -3
        ld      hl, 5
        call    __imul
        ld      de, -15
        call    @check

        ; -3 * -5 = 15
        ld      de, -3
        ld      hl, -5
        call    __imul
        ld      de, 15
        call    @check

        ; 7 * 0 = 0
        ld      de, 7
        ld      hl, 0
        call    __imul
        ld      de, 0
        call    @check

        ; 1000 * 1000 = 1000000
        ld      de, 1000
        ld      hl, 1000
        call    __imul
        ld      de, 1000000
        call    @check

        ; truncation: 0x800000 * 2 mod 2^24 = 0
        ld      de, 0x800000
        ld      hl, 2
        call    __imul
        ld      de, 0
        call    @check

        ; 1 * -1 = -1
        ld      de, 1
        ld      hl, -1
        call    __imul
        ld      de, -1
        call    @check

        ; 0x010101 * 0x010101 mod 2^24 = 0x030201 (exercises all three
        ; offset-2 partial products - P02, P11, P20 - simultaneously,
        ; the path most specific to the MLT-based partial-product
        ; algorithm; expected value computed with Python, not by hand)
        ld      de, 0x010101
        ld      hl, 0x010101
        call    __imul
        ld      de, 0x030201
        call    @check

        ; INT_MAX * INT_MAX mod 2^24 = 1
        ld      de, 0x7FFFFF
        ld      hl, 0x7FFFFF
        call    __imul
        ld      de, 1
        call    @check

        ; 0x123456 * 0x789ABC mod 2^24 = 0x2A2B28 (every byte of both
        ; operands distinct and nonzero, catching carry-propagation bugs
        ; simpler patterns could miss)
        ld      de, 0x123456
        ld      hl, 0x789ABC
        call    __imul
        ld      de, 0x2A2B28
        call    @check

        ld      hl, (fail_count)
        ld      a, l
        ld      sp, ix
        pop     ix
        out0    (0x00), a
        ld      hl, 0
        ret

; hl = actual, de = expected. Increments fail_count on mismatch.
@check:
        or      a
        sbc     hl, de
        ret     z
        ld      hl, (fail_count)
        inc     hl
        ld      (fail_count), hl
        ret

fail_count:     dl      0
