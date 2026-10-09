; rt.s test: __memset (de=dst, hl=fill value low byte, bc=count; returns
; hl=dst - contract fixed 2026-09-22 in docs/abi.md while implementing
; this file). Exits via port 0 with the number of failed cases.

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

BIG:             equ     0x060000

        include "lib\rt\rt.s"

start:
        push    ix
        ld      ix, 0
        add     ix, sp
        ld      hl, 0
        ld      (fail_count), hl

        ; fill buf[0..4] with 0xA5; buf[5] is a sentinel and must survive
        ld      a, 0xEE
        ld      (buf+5), a
        ld      de, buf
        ld      hl, 0xA5                ; only l (0xA5) is used as the fill byte
        ld      bc, 5
        call    __memset
        ld      de, buf
        call    @check                  ; returned hl must equal dst

        ld      hl, buf
        ld      b, 5
@cmp1:
        ld      a, (hl)
        cp      0xA5
        jr      z, @cmp1ok
        push    bc
        ld      hl, (fail_count)
        inc     hl
        ld      (fail_count), hl
        pop     bc
@cmp1ok:
        inc     hl
        djnz    @cmp1

        ld      a, (buf+5)
        cp      0xEE
        jr      z, @sentok
        ld      hl, (fail_count)
        inc     hl
        ld      (fail_count), hl
@sentok:

        ; zero-length fill: must not touch buf[0] and must still return dst
        ld      a, 0x33
        ld      (buf), a
        ld      de, buf
        ld      hl, 0x99
        ld      bc, 0
        call    __memset
        ld      de, buf
        call    @check
        ld      a, (buf)
        cp      0x33
        jr      z, @zeroLenOk
        ld      hl, (fail_count)
        inc     hl
        ld      (fail_count), hl
@zeroLenOk:

        ; fill value's upper bytes must be ignored: hl=0x1234A5 -> byte 0xA5
        ld      a, 0
        ld      (buf), a
        ld      de, buf
        ld      hl, 0x1234A5
        ld      bc, 1
        call    __memset
        ld      a, (buf)
        cp      0xA5
        jr      z, @hibyteOk
        ld      hl, (fail_count)
        inc     hl
        ld      (fail_count), hl
@hibyteOk:

        ; counts of 64K and more: every bit of the 24-bit count counts
        ; (BIG is free RAM between the program and the bss)
        ld      a, 0xEE
        ld      (BIG+0x10000), a
        ld      a, 0xEE
        ld      (BIG+0x10001), a
        ld      de, BIG
        ld      hl, 0x5A
        ld      bc, 0x10000
        call    __memset
        ld      de, BIG
        call    @check
        ld      hl, BIG
        ld      c, 0x5A
        call    @expect
        ld      hl, BIG+0xFFFF
        ld      c, 0x5A
        call    @expect
        ld      hl, BIG+0x10000
        ld      c, 0xEE
        call    @expect
        ld      de, BIG
        ld      hl, 0x3C
        ld      bc, 0x10001
        call    __memset
        ld      hl, BIG+0xFFFF
        ld      c, 0x3C
        call    @expect
        ld      hl, BIG+0x10000
        ld      c, 0x3C
        call    @expect
        ld      hl, BIG+0x10001
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
buf:            db      0, 0, 0, 0, 0, 0
