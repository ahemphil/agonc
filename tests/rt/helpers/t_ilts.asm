; rt.s test: __ilts = (de < hl) ? 1 : 0, signed. Includes the boundary
; case at INT_MIN/INT_MAX where the S-xor-V technique's overflow branch is
; actually exercised (a naive "check the sign of the difference" test
; would get this one wrong). Exits via port 0 with the failure count.

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

INT_MIN:         equ     -8388608
INT_MAX:         equ     8388607

        include "lib\rt\rt.s"

start:
        push    ix
        ld      ix, 0
        add     ix, sp
        ld      hl, 0
        ld      (fail_count), hl

        ; 3 < 5 = 1
        ld      de, 3
        ld      hl, 5
        call    __ilts
        ld      de, 1
        call    @check

        ; 5 < 3 = 0
        ld      de, 5
        ld      hl, 3
        call    __ilts
        ld      de, 0
        call    @check

        ; 5 < 5 = 0
        ld      de, 5
        ld      hl, 5
        call    __ilts
        ld      de, 0
        call    @check

        ; -1 < 0 = 1
        ld      de, -1
        ld      hl, 0
        call    __ilts
        ld      de, 1
        call    @check

        ; 0 < -1 = 0
        ld      de, 0
        ld      hl, -1
        call    __ilts
        ld      de, 0
        call    @check

        ; -5 < -3 = 1
        ld      de, -5
        ld      hl, -3
        call    __ilts
        ld      de, 1
        call    @check

        ; -3 < -5 = 0
        ld      de, -3
        ld      hl, -5
        call    __ilts
        ld      de, 0
        call    @check

        ; INT_MIN < INT_MAX = 1  (subtraction overflows: exercises the
        ; S-xor-V "flip" branch)
        ld      de, INT_MIN
        ld      hl, INT_MAX
        call    __ilts
        ld      de, 1
        call    @check

        ; INT_MAX < INT_MIN = 0  (same overflow, other direction)
        ld      de, INT_MAX
        ld      hl, INT_MIN
        call    __ilts
        ld      de, 0
        call    @check

        ; INT_MIN < INT_MIN = 0
        ld      de, INT_MIN
        ld      hl, INT_MIN
        call    __ilts
        ld      de, 0
        call    @check

        ld      hl, (fail_count)
        ld      a, l
        ld      sp, ix
        pop     ix
        out0    (0x00), a
        ld      hl, 0
        ret

@check:
        or      a
        sbc     hl, de
        ret     z
        ld      hl, (fail_count)
        inc     hl
        ld      (fail_count), hl
        ret

fail_count:     dl      0
