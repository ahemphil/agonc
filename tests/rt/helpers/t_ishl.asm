; rt.s test: __ishl (left shift, count in hl, value in de). Exits via
; port 0 with the number of failed cases (0 = all passed).

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

        ; 1 << 0 = 1
        ld      de, 1
        ld      hl, 0
        call    __ishl
        ld      de, 1
        call    @check

        ; 1 << 1 = 2
        ld      de, 1
        ld      hl, 1
        call    __ishl
        ld      de, 2
        call    @check

        ; 1 << 4 = 16
        ld      de, 1
        ld      hl, 4
        call    __ishl
        ld      de, 16
        call    @check

        ; 5 << 2 = 20
        ld      de, 5
        ld      hl, 2
        call    __ishl
        ld      de, 20
        call    @check

        ; 1 << 23 = 0x800000
        ld      de, 1
        ld      hl, 23
        call    __ishl
        ld      de, 0x800000
        call    @check

        ; 0 << 5 = 0
        ld      de, 0
        ld      hl, 5
        call    __ishl
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
