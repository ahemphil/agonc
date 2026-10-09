; rt.s test: __iremu (unsigned remainder). Exits via port 0 with the
; number of failed cases (0 = all passed).

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

        ; 10 % 3 = 1
        ld      de, 10
        ld      hl, 3
        call    __iremu
        ld      de, 1
        call    @check

        ; 0 % 5 = 0
        ld      de, 0
        ld      hl, 5
        call    __iremu
        ld      de, 0
        call    @check

        ; 100 % 10 = 0
        ld      de, 100
        ld      hl, 10
        call    __iremu
        ld      de, 0
        call    @check

        ; 7 % 7 = 0
        ld      de, 7
        ld      hl, 7
        call    __iremu
        ld      de, 0
        call    @check

        ; 1 % 7 = 1
        ld      de, 1
        ld      hl, 7
        call    __iremu
        ld      de, 1
        call    @check

        ; 15 % 4 = 3
        ld      de, 15
        ld      hl, 4
        call    __iremu
        ld      de, 3
        call    @check

        ; 0xFFFFFF % 2 = 1
        ld      de, 0xFFFFFF
        ld      hl, 2
        call    __iremu
        ld      de, 1
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
