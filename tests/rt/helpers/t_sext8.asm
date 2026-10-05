; rt.s test: __sext8 (sign-extend hl's low byte to a full 24-bit int).
; Exits via port 0 with the number of failed cases (0 = all passed).

        assume  adl=1
        org     0x040000

        jp      start
        align   64
        db      "MOS", 0, 1

__bss_base:      equ     0x0AFF00
__idivu_buf:     equ     __bss_base+0
__iremu_buf:     equ     __bss_base+6
__idivs_neg:     equ     __bss_base+12
__irems_neg:     equ     __bss_base+13
__ishru_buf:     equ     __bss_base+14
__ishrs_buf:     equ     __bss_base+17
__sext8_buf:     equ     __bss_base+20
__sext16_buf:    equ     __bss_base+23
__imul_buf:      equ     __bss_base+26
__imul_res:      equ     __bss_base+32

        include "lib\rt\rt.s"

start:
        push    ix
        ld      ix, 0
        add     ix, sp
        ld      hl, 0
        ld      (fail_count), hl

        ; 5 -> 5
        ld      hl, 5
        call    __sext8
        ld      de, 5
        call    @check

        ; 0 -> 0
        ld      hl, 0
        call    __sext8
        ld      de, 0
        call    @check

        ; 0x7F (127) -> 127
        ld      hl, 0x7F
        call    __sext8
        ld      de, 127
        call    @check

        ; 0x80 (-128) -> -128
        ld      hl, 0x80
        call    __sext8
        ld      de, -128
        call    @check

        ; 0xFF (-1) -> -1
        ld      hl, 0xFF
        call    __sext8
        ld      de, -1
        call    @check

        ; garbage in the upper bytes must not matter: 0x123480 -> -128
        ld      hl, 0x123480
        call    __sext8
        ld      de, -128
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
