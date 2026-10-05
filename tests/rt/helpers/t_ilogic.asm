; rt.s test: __iand / __ior / __ixor = de op hl over all 24 bits. Exits via
; port 0 with the number of failed cases (0 = all passed). Every case has a
; distinct pattern in the upper byte, which is the one only reachable
; through memory, and checks that de comes back unchanged.

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

        ; 0xF0F0F0 & 0x0FFF0F = 0x00F000
        ld      de, 0xF0F0F0
        ld      hl, 0x0FFF0F
        call    __iand
        ld      bc, 0x00F000
        call    @check
        ; de unchanged
        ex      de, hl
        ld      bc, 0xF0F0F0
        call    @check

        ; 0xFFFFFF & 0x800001 = 0x800001
        ld      de, 0xFFFFFF
        ld      hl, 0x800001
        call    __iand
        ld      bc, 0x800001
        call    @check

        ; 0x123456 & 0 = 0
        ld      de, 0x123456
        ld      hl, 0
        call    __iand
        ld      bc, 0
        call    @check

        ; 0x800001 | 0x7F0000 = 0xFF0001
        ld      de, 0x800001
        ld      hl, 0x7F0000
        call    __ior
        ld      bc, 0xFF0001
        call    @check
        ex      de, hl
        ld      bc, 0x800001
        call    @check

        ; 0x0000FF | 0xAB0000 = 0xAB00FF
        ld      de, 0x0000FF
        ld      hl, 0xAB0000
        call    __ior
        ld      bc, 0xAB00FF
        call    @check

        ; 0xFFFFFF ^ 0x123456 = 0xEDCBA9
        ld      de, 0xFFFFFF
        ld      hl, 0x123456
        call    __ixor
        ld      bc, 0xEDCBA9
        call    @check
        ex      de, hl
        ld      bc, 0xFFFFFF
        call    @check

        ; 0x5A5A5A ^ 0x5A5A5A = 0
        ld      de, 0x5A5A5A
        ld      hl, 0x5A5A5A
        call    __ixor
        ld      bc, 0
        call    @check

        ; 0x010203 ^ 0x800000 = 0x810203
        ld      de, 0x010203
        ld      hl, 0x800000
        call    __ixor
        ld      bc, 0x810203
        call    @check

        ld      hl, (fail_count)
        ld      a, l
        ld      sp, ix
        pop     ix
        out0    (0x00), a
        ld      hl, 0
        ret

; @check: hl must equal bc (hl and de are preserved for the caller)
@check:
        push    hl
        push    de
        or      a
        sbc     hl, bc
        pop     de
        pop     hl
        ret     z
        push    hl
        ld      hl, (fail_count)
        inc     hl
        ld      (fail_count), hl
        pop     hl
        ret

fail_count:     dl      0
