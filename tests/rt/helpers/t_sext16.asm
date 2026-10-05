; rt.s test: __sext16 (sign-extend hl's low 16 bits to a full 24-bit int).
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

        ; 100 -> 100
        ld      hl, 100
        call    __sext16
        ld      de, 100
        call    @check

        ; 0 -> 0
        ld      hl, 0
        call    __sext16
        ld      de, 0
        call    @check

        ; 0x7FFF (32767) -> 32767
        ld      hl, 0x7FFF
        call    __sext16
        ld      de, 32767
        call    @check

        ; 0x8000 (-32768) -> -32768
        ld      hl, 0x8000
        call    __sext16
        ld      de, -32768
        call    @check

        ; 0xFFFF (-1) -> -1
        ld      hl, 0xFFFF
        call    __sext16
        ld      de, -1
        call    @check

        ; garbage in the top byte must not matter: 0x998000 -> -32768
        ld      hl, 0x998000
        call    __sext16
        ld      de, -32768
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
