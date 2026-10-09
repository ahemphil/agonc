; rt.s test: __memcpy (de=dst, hl=src, bc=count; returns hl=dst). Exits
; via port 0 with the number of failed cases (0 = all passed).

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

SRC:             equ     0x060000
DST:             equ     0x080000

        include "lib\rt\rt.s"

start:
        push    ix
        ld      ix, 0
        add     ix, sp
        ld      hl, 0
        ld      (fail_count), hl

        ; copy 5 bytes from src to dst
        ld      de, dst
        ld      hl, src
        ld      bc, 5
        call    __memcpy
        ; returned hl must equal dst
        ld      de, dst
        call    @check

        ; dst[0..4] must now equal src[0..4]
        ld      hl, dst
        ld      de, src
        ld      b, 5
@cmp1:
        ld      a, (de)
        cp      (hl)
        jr      z, @cmp1ok
        push    bc
        ld      hl, (fail_count)
        inc     hl
        ld      (fail_count), hl
        pop     bc
@cmp1ok:
        inc     hl
        inc     de
        djnz    @cmp1

        ; dst[5] (untouched sentinel) must be unchanged
        ld      a, (dst+5)
        cp      0xEE
        jr      z, @sentok
        ld      hl, (fail_count)
        inc     hl
        ld      (fail_count), hl
@sentok:

        ; zero-length copy: must not touch dst[0] and must still return dst
        ld      a, 0x11
        ld      (dst), a
        ld      de, dst
        ld      hl, src
        ld      bc, 0
        call    __memcpy
        ld      de, dst
        call    @check
        ld      a, (dst)
        cp      0x11
        jr      z, @zeroLenOk
        ld      hl, (fail_count)
        inc     hl
        ld      (fail_count), hl
@zeroLenOk:

        ; counts of 64K and more: every bit of the 24-bit count counts
        ; (SRC and DST are free RAM between the program and the bss)
        ld      a, 0x11
        ld      (SRC), a
        ld      a, 0x77
        ld      (SRC+0xFFFF), a
        ld      a, 0x66
        ld      (SRC+0x10000), a
        ld      a, 0x00
        ld      (DST), a
        ld      a, 0x00
        ld      (DST+0xFFFF), a
        ld      a, 0xEE
        ld      (DST+0x10000), a
        ld      a, 0xEE
        ld      (DST+0x10001), a
        ld      de, DST
        ld      hl, SRC
        ld      bc, 0x10000
        call    __memcpy
        ld      de, DST
        call    @check
        ld      hl, DST
        ld      c, 0x11
        call    @expect
        ld      hl, DST+0xFFFF
        ld      c, 0x77
        call    @expect
        ld      hl, DST+0x10000
        ld      c, 0xEE
        call    @expect
        ld      de, DST
        ld      hl, SRC
        ld      bc, 0x10001
        call    __memcpy
        ld      de, DST
        call    @check
        ld      hl, DST+0x10000
        ld      c, 0x66
        call    @expect
        ld      hl, DST+0x10001
        ld      c, 0xEE
        call    @expect

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

@expect:                                ; fail unless (hl) = c
        ld      a, (hl)
        cp      c
        ret     z
        ld      hl, (fail_count)
        inc     hl
        ld      (fail_count), hl
        ret

fail_count:     dl      0
src:            db      1, 2, 3, 4, 5
dst:            db      0, 0, 0, 0, 0, 0xEE
