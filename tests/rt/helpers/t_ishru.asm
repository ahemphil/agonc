; rt.s test: __ishru (unsigned/logical right shift, count in hl, value in
; de). Exits via port 0 with the number of failed cases (0 = all passed).

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

        ; 16 >> 0 = 16
        ld      de, 16
        ld      hl, 0
        call    __ishru
        ld      de, 16
        call    @check

        ; 16 >> 4 = 1
        ld      de, 16
        ld      hl, 4
        call    __ishru
        ld      de, 1
        call    @check

        ; 0xFFFFFF >> 1 = 0x7FFFFF  (logical: fills 0, not sign)
        ld      de, 0xFFFFFF
        ld      hl, 1
        call    __ishru
        ld      de, 0x7FFFFF
        call    @check

        ; 0xFFFFFF >> 23 = 1
        ld      de, 0xFFFFFF
        ld      hl, 23
        call    __ishru
        ld      de, 1
        call    @check

        ; 1 >> 1 = 0
        ld      de, 1
        ld      hl, 1
        call    __ishru
        ld      de, 0
        call    @check

        ; 0x800000 >> 1 = 0x400000
        ld      de, 0x800000
        ld      hl, 1
        call    __ishru
        ld      de, 0x400000
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
