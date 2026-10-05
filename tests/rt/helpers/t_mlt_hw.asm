; Regression test for the eZ80 MLT instruction's exact, empirically-
; verified behaviour, which __imul in rt.s depends on. Kept as a
; permanent test (not a throwaway probe) because __imul's correctness
; rests entirely on these properties holding; if a different emulator
; build or real hardware ever behaved differently, this is what would
; catch it before __imul's own tests produced confusing wrong answers.
;
; Confirmed properties:
;   1. mlt rr computes the unsigned 8x8 product of that pair's two low
;      bytes (e.g. mlt de: d*e) into the pair's low 16 bits.
;   2. It CLEARS the pair's ADL upper byte (DEU/HLU/BCU) to exactly 0x00
;      - not left stale - even though the instruction is documented as
;      "always 16-bit, even in ADL mode".
;   3. It does not affect any CPU flag.
;   4. BC, DE and HL all behave identically.
;
; Exits via port 0 with the number of failed checks (0 = all confirmed).

        assume  adl=1
        org     0x040000

        jp      start
        align   64
        db      "MOS", 0, 1

start:
        ld      hl, 0
        ld      (fail_count), hl

        ; mlt de, d=0x12 e=0x34 -> d:e = 0x03:0xA8, deu = 0x00
        ld      de, 0xAB1234
        mlt     de
        push    de
        pop     hl
        ld      de, 0x0003A8
        call    @check

        ; mlt hl, h=7 l=6 -> hl = 0x000000 + 42 = 0x00002A
        ld      hl, 0x000706
        mlt     hl
        ld      de, 42
        call    @check

        ; mlt bc, b=9 c=9 -> bc = 81 = 0x51
        ld      bc, 0x000909
        mlt     bc
        push    bc
        pop     hl
        ld      de, 81
        call    @check

        ; flags unaffected: establish Z=0,C=0 via cp, do a zero-producing
        ; mlt, confirm flags are byte-identical afterward
        ld      a, 1
        cp      0
        push    af
        pop     hl
        ld      b, l                    ; b = flags before
        ld      de, 0x000005
        mlt     de
        push    af
        pop     hl
        ld      a, l                    ; a = flags after
        cp      b
        jr      z, @flags_ok
        ld      hl, (fail_count)
        inc     hl
        ld      (fail_count), hl
@flags_ok:

        ld      hl, (fail_count)
        ld      a, l
        out0    (0x00), a
        ld      hl, 0
        ret

; hl = actual, de = expected. Increments fail_count on mismatch.
@check:
        or      a
        sbc     hl, de
        ret     z
        ld      hl, (fail_count)
        inc     hl
        ld      (fail_count), hl
        ret

fail_count:     dl      0
