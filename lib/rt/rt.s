;;agonc-object 1
;;unit rt.s 7e3c
; ---------------------------------------------------------------------------
; rt.s - runtime helpers for Agon C. Implements docs/abi.md section 6:
; every operation the eZ80 has no direct instruction for. One code section
; per helper, each with the register contract abi.md prescribes:
;
;   binary  : left in DE, right in HL -> HL
;   shift   : value in DE, count in HL -> HL
;   unary   : HL -> HL
;   compare : left DE, right HL -> HL = 0 or 1, and Z set iff HL = 0
;
; Every helper clobbers A, F, BC, DE, IY freely (and HL as the result) and
; preserves IX and SP, per abi.md section 6 - no helper here saves/restores
; IY, since the contract explicitly does not require it.
;
; Where it sits: cc2 emits `call __imul` and the like for the operators it
; does not open-code, with a `;;ref` to the helper; ld then keeps exactly
; the helper sections a program reaches (each helper is its own section,
; so a program that never divides links no division). The float, double
; and long long helpers are C, in lib/libc/fp.c and ll.c; these are the
; ones that need register arguments and so must be assembly.
;
; Map:
;   24-bit (int):  __imul, __idivu, __iremu, __idivs, __irems, __ishl,
;                  __ishru, __ishrs, the compares __iltu ... __iles,
;                  __iand/__ior/__ixor, __sext8, __sext16
;   block moves:   __memcpy, __memset
;   32-bit (long): add, subtract, negate, complement, logic, shifts,
;                  multiply, divide, compares, int <-> long (a contract
;                  of their own, set out where they start)
;   other:         __callhl, setjmp, longjmp
;
; Scratch cells: the 24-bit helpers keep working values in fixed bss
; cells (__imul_buf, __idivu_buf, ...), one set per helper. A cell is
; shared by every call of its helper, so a helper is not reentrant, and
; a value must be read out of a cell before anything else that uses the
; cell runs (each helper below notes where). An interrupt handler written
; in C may itself multiply or shift while the main program is inside one
; of these helpers, so lib/agon/kbint.s's __handler_call saves and
; restores every one of these cells around the handler: a cell added
; here must be added there too. The 32-bit helpers take their scratch
; from the stack instead and need none of this.
;
; The `;;` lines are directives to ld, never comments
; (docs/object_format.md): `;;sect` opens a section (a bss one with its
; size and no body), `;;ref` names what the section uses.
;
; Register-invariant discipline (abi.md section 3) applies throughout:
; every 24-bit value this file builds either comes from a full-width
; immediate/memory load, or is built byte-by-byte through a 3-byte memory
; scratch cell and reloaded whole - never assembled by writing only H/L
; and leaving the inaccessible top byte (HLU/DEU/BCU) stale. The upper
; byte is reachable only through a 24-bit load, store, push or pop, or an
; instruction that writes the whole register (MLT, for one, zeroes it).
; A 16-bit `.sis` load also zeroes it, but it forms its address from
; MBASE and 16 bits, so it cannot reach an arbitrary 24-bit address;
; the helpers here use 24-bit loads of their cells instead.
; ---------------------------------------------------------------------------

; ===========================================================================
; __imul: hl = de * hl  (24x24 -> 24, truncated product)
;
; Truncated (mod 2^24) multiplication is identical whether the operands are
; taken as signed or unsigned two's complement bit patterns, so one routine
; serves both - there is no separate signed/unsigned __imul, matching the
; ABI's helper list.
;
; Uses the eZ80's hardware MLT (unsigned 8x8->16 multiply) via six partial
; byte products, schoolbook-style, rather than a 24-iteration shift-add
; loop: six multiplies and their carry chains in place of 24 rounds of
; shift, test and add.
;
; Let DE = a2:a1:a0 (a0 = E, the lowest byte; a2 = DEU, the ADL upper
; byte) and HL = b2:b1:b0 likewise. The full product's bit 0-23 depend
; only on the six terms a_i*b_j with i+j <= 2 (every term with i+j >= 3
; is weighted by 256^3 or more, a multiple of 2^24, so it contributes
; exactly 0 to the truncated result and is never computed):
;   (0,0), (0,1), (1,0)              - both bytes of the 16-bit product matter
;   (0,2), (1,1), (2,0)              - only the LOW byte matters (the high
;                                       byte would land at bit 24+)
; Each product is added into a 3-byte accumulator at its byte offset
; (i+j) via ordinary ADD/ADC carry chains - plain 8-bit arithmetic, no
; shifting required, since MLT already delivers each partial product at
; bit-position 0 and the byte OFFSET (not a bit shift) places it. Carry
; beyond the accumulator's byte 2 is simply left unstored, which is
; exactly the mod-2^24 truncation we want.
;
; MLT's exact behaviour was verified by a probe, not assumed: `mlt rr`
; computes the 8x8 product of that pair's two low bytes into its low 16
; bits and CLEARS the pair's ADL upper byte to exactly 0 (not left stale);
; it does not affect flags; BC/DE/HL all behave identically.
;
; The operand bytes are reached through the bss cell __imul_buf, since a
; register's upper byte (a2, b2) can only be read by storing the whole
; register. The right operand (HL) is stored first, because the copy of
; DE goes through HL. The accumulator is the cell __imul_res, read back
; whole at the end. Both cells are this helper's alone, but shared by
; every call of it (see the header on reentrancy).
; ===========================================================================
;;sect bss __imul_buf g 6                ; a0,a1,a2,b0,b1,b2
;;sect bss __imul_res g 3
;;sect code __imul g
;;ref __imul_buf
;;ref __imul_res
__imul:
        ld      (__imul_buf+3), hl      ; buf[3..5] = b0,b1,b2 (L,H,HLU);
                                         ; hl only read here, not yet clobbered
        push    de
        pop     hl                      ; hl = de (copy; full 24-bit)
        ld      (__imul_buf+0), hl      ; buf[0..2] = a0,a1,a2 (E,D,DEU)

        ld      hl, 0
        ld      (__imul_res), hl        ; hl low 16 + the ld below clear all 3
        xor     a
        ld      (__imul_res+2), a

        ; P00 = a0*b0, offset 0: both bytes of the 16-bit product matter.
        ; Each product below has the same shape: B = a_i, C = b_j, then
        ; `mlt bc` leaves BC = a_i*b_j; C is added into the result byte at
        ; offset i+j and B, with the carry, into the byte above it (at
        ; offset 2 only C is added: B would land at bit 24).
        ld      a, (__imul_buf+0)
        ld      b, a
        ld      a, (__imul_buf+3)
        ld      c, a
        mlt     bc
        ld      a, (__imul_res+0)
        add     a, c
        ld      (__imul_res+0), a
        ld      a, (__imul_res+1)
        adc     a, b
        ld      (__imul_res+1), a
        ld      a, (__imul_res+2)
        adc     a, 0                    ; the carry out of byte 1
        ld      (__imul_res+2), a

        ; P01 = a0*b1, offset 1 (its carry out of byte 2 is bit 24: dropped)
        ld      a, (__imul_buf+0)
        ld      b, a
        ld      a, (__imul_buf+4)
        ld      c, a
        mlt     bc
        ld      a, (__imul_res+1)
        add     a, c
        ld      (__imul_res+1), a
        ld      a, (__imul_res+2)
        adc     a, b
        ld      (__imul_res+2), a

        ; P10 = a1*b0, offset 1
        ld      a, (__imul_buf+1)
        ld      b, a
        ld      a, (__imul_buf+3)
        ld      c, a
        mlt     bc
        ld      a, (__imul_res+1)
        add     a, c
        ld      (__imul_res+1), a
        ld      a, (__imul_res+2)
        adc     a, b
        ld      (__imul_res+2), a

        ; P02 = a0*b2, offset 2 - only the low byte of the product matters
        ld      a, (__imul_buf+0)
        ld      b, a
        ld      a, (__imul_buf+5)
        ld      c, a
        mlt     bc
        ld      a, (__imul_res+2)
        add     a, c
        ld      (__imul_res+2), a

        ; P11 = a1*b1, offset 2 - low byte only
        ld      a, (__imul_buf+1)
        ld      b, a
        ld      a, (__imul_buf+4)
        ld      c, a
        mlt     bc
        ld      a, (__imul_res+2)
        add     a, c
        ld      (__imul_res+2), a

        ; P20 = a2*b0, offset 2 - low byte only
        ld      a, (__imul_buf+2)
        ld      b, a
        ld      a, (__imul_buf+3)
        ld      c, a
        mlt     bc
        ld      a, (__imul_res+2)
        add     a, c
        ld      (__imul_res+2), a

        ld      hl, (__imul_res)        ; all 24 bits at once: HLU included
        ret

; ===========================================================================
; __idivu: hl = de / hl  (unsigned, dividend=de, divisor=hl)
;
; Classic combined-register restoring binary long division. A 6-byte
; scratch buffer holds Q:R as one logical 48-bit shift register (Q = low 3
; bytes, initially the dividend, becomes the quotient; R = high 3 bytes,
; initially 0, becomes the remainder). Each of 24 iterations: shift the
; whole 48-bit Q:R left by one bit (a 6-byte SLA/RL cascade, since there is
; no direct 24-bit-pair shift instruction); then if R >= divisor, subtract
; and set the quotient's new bit (which the shift just set to 0) to 1.
; Verified by hand with a 4-bit/4-iteration trace of 13/3 = 4 remainder 1.
;
; Why 24 bits of R are enough: before round k, R holds at most the
; dividend's top k-1 bits (it is that prefix reduced mod the divisor), so
; it is below 2^(k-1) and the shift never pushes a bit out of R's top.
; Division by zero (undefined in C) never borrows, so every quotient bit
; is 1: the result is 0xFFFFFF, and __iremu's remainder is the dividend.
;
; IY points at the cell __idivu_buf (Q at +0..2, R at +3..5); DE holds
; the divisor and B counts the rounds.
; ===========================================================================
;;sect bss __idivu_buf g 6
;;sect code __idivu g
;;ref __idivu_buf
__idivu:
        ex      de, hl                  ; hl = dividend, de = divisor
        ld      (__idivu_buf+0), hl     ; Q = dividend
        ld      hl, 0
        ld      (__idivu_buf+3), hl     ; R = 0
        ld      iy, __idivu_buf
        ld      b, 24
@loop:
        ; Q:R <<= 1: SLA puts 0 in the lowest bit, each RL takes the
        ; carry from the byte below
        sla     (iy+0)
        rl      (iy+1)
        rl      (iy+2)
        rl      (iy+3)
        rl      (iy+4)
        rl      (iy+5)
        ld      hl, (iy+3)              ; hl = R
        or      a
        sbc     hl, de                  ; hl = R - divisor
        jr      c, @noSub               ; R < divisor: leave quotient bit 0
        ld      (iy+3), hl              ; R -= divisor
        set     0, (iy+0)               ; quotient bit = 1
@noSub:
        djnz    @loop
        ld      hl, (iy+0)              ; hl = quotient
        ret

; ===========================================================================
; __iremu: hl = de % hl  (unsigned) - same algorithm as __idivu, returning
; the remainder (R) instead of the quotient (Q). An independent copy of
; the loop with its own cell, __iremu_buf, rather than a subroutine shared
; with __idivu: simpler, at the cost of duplicated bytes in a program that
; uses both / and %.
; ===========================================================================
;;sect bss __iremu_buf g 6
;;sect code __iremu g
;;ref __iremu_buf
__iremu:
        ex      de, hl
        ld      (__iremu_buf+0), hl
        ld      hl, 0
        ld      (__iremu_buf+3), hl
        ld      iy, __iremu_buf
        ld      b, 24
@loop:
        sla     (iy+0)
        rl      (iy+1)
        rl      (iy+2)
        rl      (iy+3)
        rl      (iy+4)
        rl      (iy+5)
        ld      hl, (iy+3)
        or      a
        sbc     hl, de
        jr      c, @noSub
        ld      (iy+3), hl
        set     0, (iy+0)
@noSub:
        djnz    @loop
        ld      hl, (iy+3)              ; hl = remainder
        ret

; ===========================================================================
; __idivs: hl = de / hl  (signed, truncating toward zero, per C89's /).
; Takes the absolute value of both operands, divides via __idivu, then
; negates the quotient if exactly one operand was negative. Sign of a
; 24-bit value is read via a non-destructive `ld bc,0 / or a / sbc hl,bc`
; (subtracting zero changes nothing but still sets S from hl's true sign,
; including bit 23 - which is otherwise inaccessible - because the eZ80
; correctly derives S from the full 24-bit ALU result in ADL mode).
;
; The result's sign is kept in the cell __idivs_neg (1: negate the
; quotient), written before the call to __idivu and read after it;
; __idivu does not touch it. Negating is `0 - x` by `sbc hl,bc`, as the
; eZ80 has no 24-bit NEG. The most negative int, -8388608, has no
; positive counterpart, but its magnitude 0x800000 is right when the
; division treats it as unsigned, so it divides correctly (and -8388608
; / -1 wraps back to -8388608, C leaving that overflow undefined).
; ===========================================================================
;;sect bss __idivs_neg g 1
;;sect code __idivs g
;;ref __idivs_neg
;;ref __idivu
__idivs:
        xor     a
        ld      (__idivs_neg), a

        ; --- test dividend's sign (de), without disturbing de or hl ---
        push    hl
        push    de
        pop     hl                      ; hl = dividend copy; de unchanged
        ld      bc, 0
        or      a
        sbc     hl, bc                  ; hl unchanged; flags = dividend's sign
        pop     hl                      ; hl = divisor restored
        jp      p, @div_ok
        ld      a, 1
        ld      (__idivs_neg), a
@div_ok:
        ; --- test divisor's sign (hl) ---
        ld      bc, 0
        or      a
        sbc     hl, bc                  ; hl unchanged; flags = divisor's sign
        jp      p, @dvr_ok
        ld      a, (__idivs_neg)
        xor     1
        ld      (__idivs_neg), a
@dvr_ok:
        ; --- negate dividend (de) if it was negative ---
        push    hl                      ; save divisor
        push    de
        pop     hl                      ; hl = dividend copy
        ld      bc, 0
        or      a
        sbc     hl, bc                  ; hl unchanged; flags = dividend's sign
        jp      p, @div_abs_done
        push    hl
        pop     bc                      ; bc = dividend (copy)
        ld      hl, 0
        or      a
        sbc     hl, bc                  ; hl = -dividend
        ex      de, hl                  ; de = -dividend
@div_abs_done:
        pop     hl                      ; hl = divisor restored

        ; --- negate divisor (hl) if it was negative ---
        ld      bc, 0
        or      a
        sbc     hl, bc                  ; hl unchanged; flags = divisor's sign
        jp      p, @dvr_abs_done
        push    hl
        pop     bc                      ; bc = divisor (copy)
        ld      hl, 0
        or      a
        sbc     hl, bc                  ; hl = -divisor
@dvr_abs_done:

        call    __idivu                 ; de=abs(dividend), hl=abs(divisor) -> hl=quotient

        ld      a, (__idivs_neg)
        or      a
        ret     z
        push    hl
        pop     bc
        ld      hl, 0
        or      a
        sbc     hl, bc                  ; hl = -quotient
        ret

; ===========================================================================
; __irems: hl = de % hl  (signed; result takes the DIVIDEND's sign, per
; C89's %). Same abs/negate structure as __idivs, but the sign to apply to
; the result depends only on the dividend, and the core call is __iremu.
; The sign lives in the cell __irems_neg across that call.
; ===========================================================================
;;sect bss __irems_neg g 1
;;sect code __irems g
;;ref __irems_neg
;;ref __iremu
__irems:
        xor     a
        ld      (__irems_neg), a

        ; --- test dividend's sign (de) ---
        push    hl
        push    de
        pop     hl
        ld      bc, 0
        or      a
        sbc     hl, bc
        pop     hl
        jp      p, @div_ok
        ld      a, 1
        ld      (__irems_neg), a
@div_ok:
        ; --- abs divisor (hl); its sign does not affect the result's sign ---
        ld      bc, 0
        or      a
        sbc     hl, bc
        jp      p, @dvr_ok
        push    hl
        pop     bc
        ld      hl, 0
        or      a
        sbc     hl, bc
@dvr_ok:
        ; --- abs dividend (de) ---
        push    hl
        push    de
        pop     hl
        ld      bc, 0
        or      a
        sbc     hl, bc
        jp      p, @div_abs_done
        push    hl
        pop     bc
        ld      hl, 0
        or      a
        sbc     hl, bc
        ex      de, hl
@div_abs_done:
        pop     hl

        call    __iremu

        ld      a, (__irems_neg)
        or      a
        ret     z
        push    hl
        pop     bc
        ld      hl, 0
        or      a
        sbc     hl, bc
        ret

; ===========================================================================
; __ishl: hl = de << hl  (count in hl, value in de; abi.md: a count outside
; 0..23 is undefined, so the low byte of the count is used with no masking).
; `add hl,hl` doubles all 24 bits of HL, so the shift needs no scratch
; cell. One bit per round (the 24-bit shifts do not move whole bytes
; first, as the 32-bit ones below do); a count of 24 to 255 gives 0. The
; zero test comes first because DJNZ with B = 0 would run 256 times.
; ===========================================================================
;;sect code __ishl g
__ishl:
        push    hl
        pop     bc                      ; bc = count (c is what matters)
        ex      de, hl                  ; hl = value to shift
        ld      a, c
        or      a
        jr      z, @done
        ld      b, a
@loop:
        add     hl, hl                  ; 24-bit-safe left shift by 1
        djnz    @loop
@done:
        ret

; ===========================================================================
; __ishru: hl = (unsigned)de >> hl  (count in hl, value in de). There is no
; 24-bit-pair shift instruction, so the value is shifted through a 3-byte
; memory scratch, one bit at a time, cascading top-byte-first with SRL
; (fills 0) then RR (rotates the carry down through the lower bytes).
; The value goes into the cell __ishru_buf at once, so DE is free; the
; cell is read back whole at the end. One bit per round, a count of 0
; tested before DJNZ (which would otherwise run 256 times); a count of 24
; to 255 gives 0.
; ===========================================================================
;;sect bss __ishru_buf g 3
;;sect code __ishru g
;;ref __ishru_buf
__ishru:
        push    hl
        pop     bc                      ; bc = count
        ld      (__ishru_buf), de
        ld      a, c
        or      a
        jr      z, @done
        ld      iy, __ishru_buf
        ld      b, a
@loop:
        srl     (iy+2)
        rr      (iy+1)
        rr      (iy+0)
        djnz    @loop
@done:
        ld      hl, (__ishru_buf)
        ret

; ===========================================================================
; __ishrs: hl = (signed)de >> hl  (count in hl, value in de). Identical to
; __ishru except the top byte uses SRA (arithmetic: copies the sign bit
; into the new top bit) instead of SRL, preserving the sign throughout.
; Its cell is __ishrs_buf; a count of 24 to 255 gives 0 or -1.
; ===========================================================================
;;sect bss __ishrs_buf g 3
;;sect code __ishrs g
;;ref __ishrs_buf
__ishrs:
        push    hl
        pop     bc
        ld      (__ishrs_buf), de
        ld      a, c
        or      a
        jr      z, @done
        ld      iy, __ishrs_buf
        ld      b, a
@loop:
        sra     (iy+2)
        rr      (iy+1)
        rr      (iy+0)
        djnz    @loop
@done:
        ld      hl, (__ishrs_buf)
        ret

; ===========================================================================
; __iltu: hl = (de < hl) ? 1 : 0  (unsigned).
; `ex de,hl` then `or a / sbc hl,de` computes (original_de - original_hl),
; with carry set iff original_de < original_hl (unsigned) - exactly the
; comparison wanted. `ld hl,0 / rl l` then turns that carry into a clean
; canonical 0/1 in hl (h and hlu are already 0, so Z ends up correctly
; reflecting hl's final value too, per the compare contract).
; ===========================================================================
;;sect code __iltu g
__iltu:
        ex      de, hl
        or      a
        sbc     hl, de
        ld      hl, 0
        rl      l
        ret

; ===========================================================================
; __ilts: hl = (de < hl) ? 1 : 0  (signed). Same subtraction as __iltu, but
; "less" is S xor V, since a 24-bit `cp`-equivalent has no single flag for
; signed order. S alone is the sign of the difference, which is right
; unless the subtraction overflowed (V, tested as parity-even `pe`), and
; an overflow flips the sign: so less = S when V is clear and not S when
; it is set. The two true/false branches converge on `scf`/`or a` to put
; the intended boolean into carry, then the same `ld hl,0 / rl l` idiom
; as __iltu.
; ===========================================================================
;;sect code __ilts g
__ilts:
        ex      de, hl
        or      a
        sbc     hl, de                  ; flags from (original_de - original_hl)
        jp      pe, @flip
        jp      m, @true
        jr      @false
@flip:
        jp      m, @false
        jr      @true
@true:
        scf
        jr      @setresult
@false:
        or      a
@setresult:
        ld      hl, 0
        rl      l
        ret

; ===========================================================================
; __ileu: hl = (de <= hl) ? 1 : 0  (unsigned). Same subtraction as __iltu;
; true when carry (de < hl) or zero (de == hl).
; ===========================================================================
;;sect code __ileu g
__ileu:
        ex      de, hl
        or      a
        sbc     hl, de
        jr      c, @true
        jr      z, @true
        or      a
        jr      @setresult
@true:
        scf
@setresult:
        ld      hl, 0
        rl      l
        ret

; ===========================================================================
; __iles: hl = (de <= hl) ? 1 : 0  (signed). Equality is checked first
; (the subtraction result being exactly zero settles it regardless of
; overflow); otherwise the same S-xor-V test as __ilts.
; ===========================================================================
;;sect code __iles g
__iles:
        ex      de, hl
        or      a
        sbc     hl, de
        jr      z, @true
        jp      pe, @flip
        jp      m, @true
        jr      @false
@flip:
        jp      m, @false
        jr      @true
@false:
        or      a
        jr      @setresult
@true:
        scf
@setresult:
        ld      hl, 0
        rl      l
        ret

; ===========================================================================
; __iand / __ior / __ixor: hl = de & hl, de | hl, de ^ hl (all 24 bits).
; Helpers for cc2 (abi.md section 6): the eZ80's logic instructions are
; 8-bit only and the pairs' upper bytes (DEU, HLU) cannot be addressed,
; so both operands go through the stack and are combined a byte at a time
; there - 38 bytes open-coded, hence helpers. Clobber a, f, iy; de is
; left unchanged. The result is written over HL's copy on the stack, so
; `pop hl` collects it and `pop de` restores DE and balances the stack.
; ===========================================================================
;;sect code __iand g
__iand:
        push    de
        push    hl
        ld      iy, 0
        add     iy, sp                  ; iy+0..2 = hl, iy+3..5 = de
        ld      a, (iy+0)
        and     (iy+3)
        ld      (iy+0), a
        ld      a, (iy+1)
        and     (iy+4)
        ld      (iy+1), a
        ld      a, (iy+2)
        and     (iy+5)
        ld      (iy+2), a
        pop     hl
        pop     de
        ret

;;sect code __ior g
__ior:
        push    de
        push    hl
        ld      iy, 0
        add     iy, sp                  ; iy+0..2 = hl, iy+3..5 = de
        ld      a, (iy+0)
        or      (iy+3)
        ld      (iy+0), a
        ld      a, (iy+1)
        or      (iy+4)
        ld      (iy+1), a
        ld      a, (iy+2)
        or      (iy+5)
        ld      (iy+2), a
        pop     hl
        pop     de
        ret

;;sect code __ixor g
__ixor:
        push    de
        push    hl
        ld      iy, 0
        add     iy, sp                  ; iy+0..2 = hl, iy+3..5 = de
        ld      a, (iy+0)
        xor     (iy+3)
        ld      (iy+0), a
        ld      a, (iy+1)
        xor     (iy+4)
        ld      (iy+1), a
        ld      a, (iy+2)
        xor     (iy+5)
        ld      (iy+2), a
        pop     hl
        pop     de
        ret

; ===========================================================================
; __sext8: hl = sign-extend(l)  - treats hl's low byte as a signed char and
; produces its 24-bit value. HLU is not directly writable, so the result is
; built byte-by-byte in a 3-byte scratch cell and reloaded whole.
; `rlca` / `sbc a,a` is the sign-smear idiom: RLCA copies bit 7 into the
; carry, and A - A - carry is 0 or 0xFF by that carry alone.
; ===========================================================================
;;sect bss __sext8_buf g 3
;;sect code __sext8 g
;;ref __sext8_buf
__sext8:
        ld      a, l
        ld      (__sext8_buf), a        ; buf[0] = original low byte
        rlca                            ; bit 7 -> carry
        sbc     a, a                    ; a = 0xFF if negative, else 0x00
        ld      (__sext8_buf+1), a      ; buf[1] = extension byte
        ld      (__sext8_buf+2), a      ; buf[2] = extension byte (HLU)
        ld      hl, (__sext8_buf)
        ret

; ===========================================================================
; __sext16: hl = sign-extend(h:l) - treats the low 16 bits of hl as a
; signed short; only the (inaccessible) top byte HLU needs fixing up.
; ===========================================================================
;;sect bss __sext16_buf g 3
;;sect code __sext16 g
;;ref __sext16_buf
__sext16:
        ld      a, l
        ld      (__sext16_buf+0), a
        ld      a, h
        ld      (__sext16_buf+1), a
        rlca                            ; bit 7 of the original h -> carry
        sbc     a, a                    ; a = 0xFF if negative, else 0x00
        ld      (__sext16_buf+2), a
        ld      hl, (__sext16_buf)
        ret

; ===========================================================================
; __memcpy: de=dst, hl=src, bc=count; returns hl=dst. LDIR does the copy
; directly (hl=src, de=dst, bc=count is exactly its own register usage);
; dst is saved across it since LDIR leaves hl/de advanced past the copy.
; A count of 0 must be checked before LDIR runs at all: LDIR decrements
; bc first and repeats while bc != 0, so bc=0 on entry would wrap to
; 0xFFFFFF and copy nearly 16 million bytes instead of none (a zero-length
; copy then hangs the program or overwrites all of memory). The check
; tests all 24 bits of BC, as LDIR uses them ("ld a,b / or c" would take
; a count that is a multiple of 64K for 0): 0 - BC is zero only when BC
; is, and HL is kept on the stack around it.
; cc2 calls this to copy structs and unions (assignment, arguments by
; value, returns) and to store a double or long long (8 bytes).
; ===========================================================================
;;sect code __memcpy g
__memcpy:
        push    hl
        ld      hl, 0
        or      a
        sbc     hl, bc                  ; Z iff bc = 0
        pop     hl                      ; (pop leaves the flags alone)
        jr      nz, @copy
        ex      de, hl                  ; hl = dst already; nothing to copy
        ret
@copy:
        push    de                      ; save dst for the return value
        ldir
        pop     hl                      ; hl = dst
        ret

; ===========================================================================
; __memset: de=dst, hl=fill value (low byte used), bc=count; returns
; hl=dst (abi.md section 6; the same register shape as __memcpy's).
; The fill is LDIR's self-propagating one (as crt0.s step 3): the first
; byte is stored, then LDIR copies each byte to the one after it. The
; count is tested for 0, and after the first byte for 0 again, on all 24
; bits of BC (0 - BC is zero only when BC is), since LDIR with BC = 0
; would run 16M times.
; ===========================================================================
;;sect code __memset g
__memset:
        push    de                      ; save dst for the return value
        ld      a, l                    ; a = the fill byte
        ld      hl, 0
        or      a
        sbc     hl, bc                  ; Z iff count = 0
        jr      z, @done
        ld      (de), a                 ; the first byte
        dec     bc
        ld      hl, 0
        or      a
        sbc     hl, bc                  ; Z iff count was 1
        jr      z, @done
        push    de
        pop     hl                      ; hl = dst
        inc     de                      ; de = dst+1
        ldir
@done:
        pop     hl                      ; hl = dst
        ret

; ===========================================================================
; 32-bit helpers (abi.md sections 3 and 6). A long or unsigned long is
; held in E:UHL: HL the low 24 bits, E the high byte; D and DEU are
; unspecified. A binary helper takes its left operand in the secondary,
; A:UBC (BC the low 24 bits, A the high byte), its right operand in the
; primary, E:UHL, and returns E:UHL - the 24-bit helpers' "left in the
; secondary, right in the primary" shape, which suits cc2's evaluation order. A shift takes the
; value in A:UBC and the count in HL (only L is used; counts of 32 or more
; are undefined, as in C). A compare returns HL = 0 or 1 with Z set iff
; HL = 0. They clobber A, F, BC, D, IY and preserve IX and SP.
;
; Scratch space comes from the stack (IY points at it), not from bss, so
; the helpers nest and need no equates in the tests that include rt.s.
; The frame idiom: `ld iy,-N` / `add iy,sp` / `ld sp,iy` claims N bytes
; below SP with IY at their base, and `ld iy,N` / `add iy,sp` / `ld sp,iy`
; gives them back before `ret`, which then finds the return address on
; top of the stack again. A 32-bit value is stored as four
; little-endian bytes: `ld (iy+k),bc` (or hl) writes the low three, then
; A (or E) goes to iy+k+3.
;
; Shifts here move whole bytes first, eight bits at a time with one
; 24-bit load and store, then shift the rest a bit at a time; the 24-bit
; shifts above go a bit at a time throughout.
; ===========================================================================

; __ladd: E:UHL = A:UBC + E:UHL. The 24-bit `add hl,bc` carries out of
; bit 23, and `adc a,e` takes that carry into the top byte: one carry
; chain across 32 bits.
;;sect code __ladd g
__ladd:
        add     hl, bc                  ; low 24 bits; carry out of bit 23
        adc     a, e                    ; high byte, with that carry
        ld      e, a
        ret

; __lsub: E:UHL = A:UBC - E:UHL. The left operand has to be in HL for
; `sbc hl,bc`, so the low parts swap through the stack: `push bc` /
; `ex (sp),hl` / `pop bc` exchanges HL and BC (there is no `ex bc,hl`).
;;sect code __lsub g
__lsub:
        push    bc
        ex      (sp), hl                ; hl = left low, (sp) = right low
        pop     bc                      ; bc = right low
        or      a
        sbc     hl, bc                  ; low 24 bits of left - right; borrow in C
        sbc     a, e                    ; high byte: left - right - borrow
        ld      e, a
        ret

; __lneg: E:UHL = -E:UHL, as 0 - x: the low 24 bits first, then the top
; byte with their borrow (`ld a,0` leaves the carry alone). Clobbers A,
; F and BC only; __ldivs and __lrems rely on that, keeping IY and their
; stack frame across it.
;;sect code __lneg g
__lneg:
        push    hl
        pop     bc
        ld      hl, 0
        or      a
        sbc     hl, bc                  ; hl = -low; C set unless low was 0
        ld      a, 0
        sbc     a, e
        ld      e, a
        ret

; __lcpl: E:UHL = ~E:UHL. The low 24 bits as 0xFFFFFF - x, since CPL
; works on A alone.
;;sect code __lcpl g
__lcpl:
        push    hl
        pop     bc
        ld      hl, 0xFFFFFF
        or      a
        sbc     hl, bc                  ; all ones minus x is ~x, and never borrows
        ld      a, e
        cpl
        ld      e, a
        ret

; __land, __lor, __lxor: E:UHL = A:UBC op E:UHL, byte by byte through the
; stack (the eZ80's logic instructions are 8-bit only). Frame: the left
; operand at 0..3, the right at 4..7; the result overwrites bytes 0..2,
; read back whole into HL, and its top byte goes straight to E.
;;sect code __land g
__land:
        ld      iy, -8
        add     iy, sp
        ld      sp, iy
        ld      (iy+0), bc
        ld      (iy+3), a
        ld      (iy+4), hl
        ld      (iy+7), e
        ld      a, (iy+0)
        and     (iy+4)
        ld      (iy+0), a
        ld      a, (iy+1)
        and     (iy+5)
        ld      (iy+1), a
        ld      a, (iy+2)
        and     (iy+6)
        ld      (iy+2), a
        ld      a, (iy+3)
        and     (iy+7)
        ld      e, a
        ld      hl, (iy+0)
        ld      iy, 8
        add     iy, sp
        ld      sp, iy
        ret

;;sect code __lor g
__lor:
        ld      iy, -8
        add     iy, sp
        ld      sp, iy
        ld      (iy+0), bc
        ld      (iy+3), a
        ld      (iy+4), hl
        ld      (iy+7), e
        ld      a, (iy+0)
        or      (iy+4)
        ld      (iy+0), a
        ld      a, (iy+1)
        or      (iy+5)
        ld      (iy+1), a
        ld      a, (iy+2)
        or      (iy+6)
        ld      (iy+2), a
        ld      a, (iy+3)
        or      (iy+7)
        ld      e, a
        ld      hl, (iy+0)
        ld      iy, 8
        add     iy, sp
        ld      sp, iy
        ret

;;sect code __lxor g
__lxor:
        ld      iy, -8
        add     iy, sp
        ld      sp, iy
        ld      (iy+0), bc
        ld      (iy+3), a
        ld      (iy+4), hl
        ld      (iy+7), e
        ld      a, (iy+0)
        xor     (iy+4)
        ld      (iy+0), a
        ld      a, (iy+1)
        xor     (iy+5)
        ld      (iy+1), a
        ld      a, (iy+2)
        xor     (iy+6)
        ld      (iy+2), a
        ld      a, (iy+3)
        xor     (iy+7)
        ld      e, a
        ld      hl, (iy+0)
        ld      iy, 8
        add     iy, sp
        ld      sp, iy
        ret

; __lshl, __lshru, __lshrs: E:UHL = A:UBC shifted by L bits (left; right
; logical; right arithmetic). Whole bytes first, eight bits at a time with
; one 24-bit move, then up to seven single bits: `x >> 31` is three byte
; moves and seven rounds of four rotates, not 31 rounds (the soft float
; code in fp.c makes many such shifts). A count of 0 is checked before
; DJNZ, which would otherwise run 256 times; counts of 32 or more give 0
; (or the sign). The value sits in a 4-byte frame at IY; the count stays
; in A (C in __lshrs, where A is needed for the sign).
; A byte move is one overlapping 24-bit load and store: for a left shift,
; bytes 0..2 are loaded and stored at 1..3 (old byte 3 falls off) and 0
; goes into byte 0; for a right shift, bytes 1..3 go to 0..2 and byte 3
; gets 0 or the sign.
;;sect code __lshl g
__lshl:
        ld      iy, -4
        add     iy, sp
        ld      sp, iy
        ld      (iy+0), bc
        ld      (iy+3), a
        ld      a, l
@bytes:
        cp      8
        jr      c, @bits
        ld      hl, (iy+0)              ; bytes 0-2 up to 1-3
        ld      (iy+1), hl
        ld      (iy+0), 0
        sub     8
        jr      @bytes
@bits:
        or      a
        jr      z, @done
        ld      b, a
@loop:
        sla     (iy+0)
        rl      (iy+1)
        rl      (iy+2)
        rl      (iy+3)
        djnz    @loop
@done:
        ld      hl, (iy+0)
        ld      e, (iy+3)
        ld      iy, 4
        add     iy, sp
        ld      sp, iy
        ret

;;sect code __lshru g
__lshru:
        ld      iy, -4
        add     iy, sp
        ld      sp, iy
        ld      (iy+0), bc
        ld      (iy+3), a
        ld      a, l
@bytes:
        cp      8
        jr      c, @bits
        ld      hl, (iy+1)              ; bytes 1-3 down to 0-2
        ld      (iy+0), hl
        ld      (iy+3), 0
        sub     8
        jr      @bytes
@bits:
        or      a
        jr      z, @done
        ld      b, a
@loop:
        srl     (iy+3)
        rr      (iy+2)
        rr      (iy+1)
        rr      (iy+0)
        djnz    @loop
@done:
        ld      hl, (iy+0)
        ld      e, (iy+3)
        ld      iy, 4
        add     iy, sp
        ld      sp, iy
        ret

;;sect code __lshrs g
__lshrs:
        ld      iy, -4
        add     iy, sp
        ld      sp, iy
        ld      (iy+0), bc
        ld      (iy+3), a
        ld      c, l
@bytes:
        ld      a, c
        cp      8
        jr      c, @bits
        ld      hl, (iy+1)              ; bytes 1-3 down to 0-2
        ld      (iy+0), hl
        ld      a, (iy+2)               ; the sign into byte 3
        add     a, a                    ; bit 7 to the carry
        sbc     a, a                    ; 0 or 0xFF by the carry
        ld      (iy+3), a
        ld      a, c
        sub     8
        ld      c, a
        jr      @bytes
@bits:
        or      a
        jr      z, @done
        ld      b, a
@loop:
        sra     (iy+3)
        rr      (iy+2)
        rr      (iy+1)
        rr      (iy+0)
        djnz    @loop
@done:
        ld      hl, (iy+0)
        ld      e, (iy+3)
        ld      iy, 4
        add     iy, sp
        ld      sp, iy
        ret

; __lmul: E:UHL = A:UBC * E:UHL, truncated to 32 bits (the same for signed
; and unsigned). Shift and add: the multiplier (the right operand) is
; shifted right one bit at a time, and each set bit adds the multiplicand,
; which doubles every round. Scratch: m = 0..3 (the multiplicand, the
; left operand), n = 4..7 (the multiplier), result = 8..11. Bits that
; the doubling pushes out of m's top byte are dropped: they belong above
; bit 31. Always 32 rounds, whatever the operands.
;;sect code __lmul g
__lmul:
        ld      iy, -12
        add     iy, sp
        ld      sp, iy
        ld      (iy+0), bc
        ld      (iy+3), a
        ld      (iy+4), hl
        ld      (iy+7), e
        ld      hl, 0
        ld      (iy+8), hl
        ld      (iy+11), l
        ld      b, 32
@loop:
        srl     (iy+7)
        rr      (iy+6)
        rr      (iy+5)
        rr      (iy+4)                  ; C = the multiplier's lowest bit
        jr      nc, @noadd
        ld      a, (iy+8)
        add     a, (iy+0)
        ld      (iy+8), a
        ld      a, (iy+9)
        adc     a, (iy+1)
        ld      (iy+9), a
        ld      a, (iy+10)
        adc     a, (iy+2)
        ld      (iy+10), a
        ld      a, (iy+11)
        adc     a, (iy+3)
        ld      (iy+11), a
@noadd:
        sla     (iy+0)
        rl      (iy+1)
        rl      (iy+2)
        rl      (iy+3)
        djnz    @loop
        ld      hl, (iy+8)
        ld      e, (iy+11)
        ld      iy, 12
        add     iy, sp
        ld      sp, iy
        ret

; __ldivmodu: E:UHL = A:UBC / E:UHL and A:UBC = A:UBC % E:UHL, unsigned.
; Restoring division, one quotient bit per round: shift the 64-bit pair
; (R, Q) left, then subtract the divisor from R if it fits ("restoring":
; the trial difference is kept only when it does not borrow). The bit
; shifted out of R is kept in register D: when it is set, R certainly
; exceeds the divisor and the subtraction happens regardless of the
; byte-wise borrow. That bit is in fact always 0: before round k, R holds
; at most the dividend's top k-1 bits, so it is below 2^(k-1) and the
; shifted R fits in 32 bits (the same bound as __idivu's); the test only
; costs a few cycles a round. Division by zero is undefined (it gives a
; quotient of all ones and the dividend as remainder).
; Scratch: Q = 0..3 (the dividend, becoming the quotient), R = 4..7,
; the divisor = 8..11, R - divisor = 12..14 (its top byte stays in A).
; Returns both results, so __ldivu, __lremu, __ldivs and __lrems all
; share this one loop.
;;sect code __ldivmodu g
__ldivmodu:
        ld      iy, -15
        add     iy, sp
        ld      sp, iy
        ld      (iy+0), bc
        ld      (iy+3), a
        ld      (iy+8), hl
        ld      (iy+11), e
        ld      hl, 0
        ld      (iy+4), hl
        ld      (iy+7), l
        ld      b, 32
@loop:
        sla     (iy+0)
        rl      (iy+1)
        rl      (iy+2)
        rl      (iy+3)
        rl      (iy+4)
        rl      (iy+5)
        rl      (iy+6)
        rl      (iy+7)
        ld      d, 0                    ; ld leaves the carry alone
        rl      d                       ; d = the bit shifted out of R
        ; trial R - divisor, byte by byte with the borrow chained by SBC
        ld      a, (iy+4)
        sub     (iy+8)
        ld      (iy+12), a
        ld      a, (iy+5)
        sbc     a, (iy+9)
        ld      (iy+13), a
        ld      a, (iy+6)
        sbc     a, (iy+10)
        ld      (iy+14), a
        ld      a, (iy+7)
        sbc     a, (iy+11)
        jr      nc, @sub
        bit     0, d
        jr      z, @nosub               ; R < divisor, and R fitted in 32 bits
@sub:
        ld      (iy+7), a               ; R = R - divisor
        ld      a, (iy+12)
        ld      (iy+4), a
        ld      a, (iy+13)
        ld      (iy+5), a
        ld      a, (iy+14)
        ld      (iy+6), a
        set     0, (iy+0)               ; this quotient bit is 1
@nosub:
        djnz    @loop
        ld      hl, (iy+0)
        ld      e, (iy+3)
        ld      bc, (iy+4)
        ld      a, (iy+7)
        ld      iy, 15
        add     iy, sp
        ld      sp, iy
        ret

; __ldivu: E:UHL = A:UBC / E:UHL, unsigned. A tail jump: __ldivmodu's
; quotient is already in E:UHL, and the remainder it leaves in A:UBC is
; in registers a 32-bit helper may clobber.
;;sect code __ldivu g
;;ref __ldivmodu
__ldivu:
        jp      __ldivmodu

; __lremu: E:UHL = A:UBC % E:UHL, unsigned
;;sect code __lremu g
;;ref __ldivmodu
__lremu:
        call    __ldivmodu
        push    bc
        pop     hl
        ld      e, a
        ret

; __ldivs: E:UHL = A:UBC / E:UHL, signed, truncating toward zero (C89's /).
; Divides the magnitudes, then negates the quotient if exactly one operand
; was negative. The most negative long divided by -1 overflows (undefined).
; Scratch: the left operand = 0..3, the quotient's sign (bit 7) = 4.
; The left operand is saved to the frame first, because __lneg clobbers
; A:UBC; |right| waits on the stack while |left| is made. __lneg keeps
; IY, but __ldivmodu makes its own frame with IY, so IY is set again
; from SP before the sign byte is read.
;;sect code __ldivs g
;;ref __lneg
;;ref __ldivmodu
__ldivs:
        ld      iy, -5
        add     iy, sp
        ld      sp, iy
        ld      (iy+0), bc
        ld      (iy+3), a
        xor     e
        ld      (iy+4), a               ; bit 7: the signs differ
        bit     7, e
        call    nz, __lneg              ; E:UHL = |right|
        push    hl
        push    de
        ld      hl, (iy+0)
        ld      e, (iy+3)
        bit     7, e
        call    nz, __lneg              ; E:UHL = |left|
        push    hl
        pop     bc
        ld      a, e                    ; A:UBC = |left|
        pop     de
        pop     hl                      ; E:UHL = |right|
        call    __ldivmodu              ; E:UHL = |left| / |right|
        ld      iy, 0
        add     iy, sp                  ; IY = this frame again
        bit     7, (iy+4)
        call    nz, __lneg
        ld      iy, 5
        add     iy, sp
        ld      sp, iy
        ret

; __lrems: E:UHL = A:UBC % E:UHL, signed: the remainder has the sign of the
; dividend (C89's %, given / truncating toward zero).
; Scratch: the left operand = 0..3, the dividend's sign (bit 7) = 4.
; The same steps as __ldivs, taking the remainder (A:UBC) instead.
;;sect code __lrems g
;;ref __lneg
;;ref __ldivmodu
__lrems:
        ld      iy, -5
        add     iy, sp
        ld      sp, iy
        ld      (iy+0), bc
        ld      (iy+3), a
        ld      (iy+4), a               ; bit 7: the dividend is negative
        bit     7, e
        call    nz, __lneg              ; E:UHL = |right|
        push    hl
        push    de
        ld      hl, (iy+0)
        ld      e, (iy+3)
        bit     7, e
        call    nz, __lneg              ; E:UHL = |left|
        push    hl
        pop     bc
        ld      a, e                    ; A:UBC = |left|
        pop     de
        pop     hl                      ; E:UHL = |right|
        call    __ldivmodu              ; A:UBC = |left| % |right|
        push    bc
        pop     hl
        ld      e, a                    ; E:UHL = the remainder's magnitude
        ld      iy, 0
        add     iy, sp
        bit     7, (iy+4)
        call    nz, __lneg
        ld      iy, 5
        add     iy, sp
        ld      sp, iy
        ret

; Comparisons: HL = (A:UBC op E:UHL) ? 1 : 0, Z set iff HL = 0.
; Each subtracts in two steps, `sbc hl,bc` on the low 24 bits and `sbc a`
; on the top byte with the borrow, and then reads the flags of the whole
; 32-bit difference. `ld hl,0` / `ld l,a` makes the 0/1 canonical, and the
; final `or a` sets Z from A (the LD instructions set no flags). After
; the two steps Z reflects the top byte alone, so no compare reads it for
; the whole value: the <= forms compute right - left and invert the
; "less", which needs only the carry (or S and V).
; __lltu: unsigned <. left - right borrows exactly when left < right.
;;sect code __lltu g
__lltu:
        push    bc
        ex      (sp), hl                ; hl = left low
        pop     bc                      ; bc = right low
        or      a
        sbc     hl, bc
        sbc     a, e                    ; C = left < right
        ld      a, 0
        adc     a, a                    ; a = C
        ld      hl, 0
        ld      l, a
        or      a
        ret

; __lleu: unsigned <=, as not (right < left): right - left borrows exactly
; when right < left.
;;sect code __lleu g
__lleu:
        ld      d, a                    ; d = left high
        or      a
        sbc     hl, bc                  ; right low - left low
        ld      a, e
        sbc     a, d                    ; C = right < left
        ld      a, 0
        adc     a, a
        xor     1                       ; a = left <= right
        ld      hl, 0
        ld      l, a
        or      a
        ret

; __llts: signed <. After left - right, the high byte's S and V flags are
; those of the whole 32-bit subtraction, and left < right = S xor V.
;;sect code __llts g
__llts:
        push    bc
        ex      (sp), hl
        pop     bc
        or      a
        sbc     hl, bc
        sbc     a, e
        ld      a, 0
        jp      po, @nov
        jp      m, @done                ; overflow, S = 1: not less
        inc     a                       ; overflow, S = 0: less
        jr      @done
@nov:
        jp      p, @done                ; no overflow, S = 0: not less
        inc     a                       ; no overflow, S = 1: less
@done:
        ld      hl, 0
        ld      l, a
        or      a
        ret

; __lles: signed <=, as not (right < left), from right - left's S xor V.
;;sect code __lles g
__lles:
        ld      d, a
        or      a
        sbc     hl, bc                  ; right low - left low
        ld      a, e
        sbc     a, d                    ; flags of right - left
        ld      a, 0
        jp      po, @nov
        jp      p, @done                ; overflow, S = 0: right < left, so not <=
        inc     a                       ; overflow, S = 1: left <= right
        jr      @done
@nov:
        jp      m, @done                ; no overflow, S = 1: right < left
        inc     a                       ; no overflow, S = 0: left <= right
@done:
        ld      hl, 0
        ld      l, a
        or      a
        ret

; __leq: ==. Tests the low 24 bits, then the top bytes, separately, as
; the Z of a two-step subtraction would cover the top byte alone.
;;sect code __leq g
__leq:
        ld      d, a
        or      a
        sbc     hl, bc                  ; zero iff the low parts are equal
        ld      a, 0
        jr      nz, @done
        ld      a, d
        cp      e                       ; zero iff the high bytes are equal
        ld      a, 0
        jr      nz, @done
        inc     a
@done:
        ld      hl, 0
        ld      l, a
        or      a
        ret

; __lnz: HL = (E:UHL != 0), Z set iff HL = 0 (a long as a condition, and !).
;;sect code __lnz g
__lnz:
        ld      bc, 0
        or      a
        sbc     hl, bc                  ; Z iff the low 24 bits are 0
        ld      a, 0
        jr      nz, @one
        or      e                       ; a = e: Z iff the high byte is 0 too
        jr      z, @done
@one:
        ld      a, 1
@done:
        ld      hl, 0
        ld      l, a
        or      a
        ret

; Conversions between int and long.
; __itol: E:UHL = HL sign-extended (HL is a canonical int). The sign is
; bit 23, which only a 24-bit operation can see: `sbc hl,bc` with BC = 0
; sets S from it and leaves HL as it was.
;;sect code __itol g
__itol:
        push    hl
        ld      bc, 0
        or      a
        sbc     hl, bc                  ; S from all 24 bits of hl
        pop     hl
        ld      e, 0
        ret     p
        ld      e, 0xFF
        ret

; __utol: E:UHL = HL zero-extended (an unsigned int, or a pointer).
;;sect code __utol g
__utol:
        ld      e, 0
        ret

; __ltoi: HL = the low 24 bits of E:UHL; they are already in HL.
;;sect code __ltoi g
__ltoi:
        ret

; ===========================================================================
; __callhl: call the function whose address is in hl (abi.md section 6). The
; eZ80 has no "call (hl)", so cc2 calls through a pointer as "call __callhl",
; whose jp (hl) enters the function with the caller's return address on
; the stack. The function's own `ret` then goes straight back to the
; caller of __callhl. Used for calls through function pointers.
; ===========================================================================
;;sect code __callhl g
__callhl:
        jp      (hl)

; ===========================================================================
; setjmp and longjmp (<setjmp.h>, C89 4.6). A jmp_buf holds
; setjmp's return address, IX and the SP it was entered with (pointing at
; that return address). longjmp restores IX, leaves SP as setjmp's own ret
; would have, and returns to the saved address with HL = val (1 if val is
; 0), so the caller's argument cleanup follows as after a normal return.
; Nothing else needs saving: generated code keeps no value in a register
; from one statement to the next (abi.md section 3).
;
; jmp_buf layout: env+0 the return address, env+3 IX, env+6 SP. These
; are C-callable functions (stack arguments, abi.md section 4), not
; register-contract helpers. setjmp pops its return address and env to
; read them, then pushes both back, since the caller removes the
; arguments. The return address is kept in env, not reread from the
; stack, because by the time of the longjmp that slot may hold something
; else. As C requires, longjmp is valid only while the function that
; called setjmp has not returned.
; ===========================================================================
;;sect code _setjmp g
_setjmp:
        pop     bc                      ; the return address
        pop     hl                      ; env
        push    hl
        push    bc
        ld      (hl), bc
        inc     hl
        inc     hl
        inc     hl
        ld      (hl), ix
        inc     hl
        inc     hl
        inc     hl
        ex      de, hl                  ; de = &env[2]
        ld      hl, 0
        add     hl, sp                  ; hl = SP at entry
        ex      de, hl
        ld      (hl), de
        ld      hl, 0                   ; setjmp returns 0 when called
        ret

;;sect code _longjmp g
_longjmp:
        pop     bc                      ; longjmp's own return address: unused
        pop     de                      ; env
        pop     hl                      ; val
        ld      bc, 0
        or      a
        sbc     hl, bc
        jr      nz, @val
        inc     hl                      ; longjmp(env, 0) makes setjmp return 1
@val:
        ex      de, hl                  ; de = val, hl = env
        ld      bc, (hl)                ; setjmp's return address
        inc     hl
        inc     hl
        inc     hl
        ld      ix, (hl)
        inc     hl
        inc     hl
        inc     hl
        ld      hl, (hl)                ; SP at setjmp's entry
        ld      sp, hl
        ex      de, hl                  ; hl = val
        pop     de                      ; SP as after setjmp's ret (the slot
                                         ; may have been overwritten since)
        push    bc
        ret

;;end
