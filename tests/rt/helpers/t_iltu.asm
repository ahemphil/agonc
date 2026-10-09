; rt.s test: __iltu = (de < hl) ? 1 : 0, unsigned. Exits via port 0 with
; the number of failed cases (0 = all passed). Checks both the result
; value and that Z is set iff the result is 0 (per abi.md's compare
; contract) via `jr z`/`jr nz` right after the call.

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

        ; 3 < 5 = 1
        ld      de, 3
        ld      hl, 5
        call    __iltu
        ld      de, 1
        call    @check

        ; 5 < 3 = 0
        ld      de, 5
        ld      hl, 3
        call    __iltu
        ld      de, 0
        call    @check

        ; 5 < 5 = 0
        ld      de, 5
        ld      hl, 5
        call    __iltu
        ld      de, 0
        call    @check

        ; 0 < 1 = 1
        ld      de, 0
        ld      hl, 1
        call    __iltu
        ld      de, 1
        call    @check

        ; 0xFFFFFF < 1 = 0
        ld      de, 0xFFFFFF
        ld      hl, 1
        call    __iltu
        ld      de, 0
        call    @check

        ; 1 < 0xFFFFFF = 1
        ld      de, 1
        ld      hl, 0xFFFFFF
        call    __iltu
        ld      de, 1
        call    @check

        ; Z-flag contract check: a false result (hl=0) must set Z
        ld      de, 5
        ld      hl, 3
        call    __iltu
        jr      z, @zok1
        call    @bump_fail
@zok1:
        ; a true result (hl=1, nonzero) must clear Z
        ld      de, 3
        ld      hl, 5
        call    __iltu
        jr      nz, @zok2
        call    @bump_fail
@zok2:

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
@bump_fail:
        push    hl
        ld      hl, (fail_count)
        inc     hl
        ld      (fail_count), hl
        pop     hl
        ret

fail_count:     dl      0
