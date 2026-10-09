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
; Scratch cells: the 24-bit shifts and sign extensions keep working
; values in fixed bss cells (__ishru_buf, ...), one set per helper (__imul
; and the divide helpers have none: they work in registers and on the
; stack). A cell is shared by every call of its helper, so a helper is
; not reentrant, and a value must be read out of a cell before anything
; else that uses the cell runs (each helper below notes where). An
; interrupt handler written in C may itself shift while the main program
; is inside one of these helpers, so lib/agon/kbint.s's __handler_call
; saves and restores every one of these cells around the handler: a cell
; added here must be added there too. The 32-bit helpers take their
; scratch from the stack instead and need none of this.
;
; The C of the multiply and divide helpers, by the same steps, is
; lib/rt/rt_ref.c; tests/rt/test_rt.py checks the helpers against it.
;
; The `;;` lines are directives to ld, never comments
; (docs/object_format.md): `;;sect` opens a section (a bss one with its
; size and no body), `;;ref` names what the section uses.
;
; Register-invariant discipline (abi.md section 3) applies throughout:
; every 24-bit value this file builds either comes from a full-width
; immediate/memory load or pop (or whole out of MLT), or is built
; byte-by-byte through a 3-byte memory or stack scratch cell and reloaded
; whole - never assembled by writing only H/L
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
; Let DE = a2:a1:a0 (a0 = E, the lowest byte; a2 = DEU, the ADL upper
; byte) and HL = b2:b1:b0 likewise. The full product's bits 0-23 depend
; only on the six byte products a_i*b_j with i+j <= 2 (every term with
; i+j >= 3 is weighted by 256^3 or more, a multiple of 2^24):
;
;   a*b mod 2^24 = P00 + 256*X + 65536*S
;     P00 = a0*b0                   both bytes matter
;     X   = a0*b1 + a1*b0           only its low 16 bits matter
;     S   = a0*b2 + a1*b1 + a2*b0   only its low 8 bits matter
;
; Each byte product is one eZ80 MLT (`mlt rr`: rr's two low bytes
; multiplied, 8x8 -> 16, into the whole of rr; its upper byte is cleared
; to 0 and no flag changes - verified by a probe). Everything stays in
; registers: S is summed in A (8-bit adds, so its carries out of bit 7,
; worth 2^24, fall away by themselves); X is one `add hl,bc` of two MLT
; results; and the three parts are combined in HL as
;   HL = ((X + 256*S) << 8) + P00
; with S added into H, eight `add hl,hl` for the shift (whatever was in
; HLU and the carry above it is shifted out past bit 23) and P00 added
; last from DE.
;
; The upper bytes a2 and b2 are the only awkward part: no instruction
; reads a register's upper byte, so both operands are pushed and their
; bytes popped back two pairs at a time, from where the pushes leave them:
;
;   dec sp, dec sp, push hl, push de   (S = SP on entry)
;     S-8 S-7 S-6 S-5 S-4 S-3 S-2 S-1
;      a0  a1  a2  b0  b1  b2   ?   ?
;   inc sp, inc sp: SP = S-6, and `pop bc` gives C = a2, B = b0, ready
;   for a2*b0; SP = S-3, and `pop bc` gives C = b2 (B is replaced by a0
;   from E before a0*b2). SP is then back at S.
;
; The two bytes skipped by `dec sp` are never read; BCU after each pop is
; whatever byte came with it, but MLT replaces all of BC. SP never rises
; above S (the return address) and every byte is read before SP passes
; above it, so an interrupt at any point lands below everything live: no
; scratch cell is used, and __imul is reentrant (lib/agon/kbint.s has no
; cell of it to save).
;
; Register-invariant discipline: B, C, D and H are written alone only to
; feed an MLT, which rewrites the whole pair; BC, DE and HL come out of
; MLT with upper bytes 0, so `add hl,bc`, the shifts and the final
; `add hl,de` act on full, well-defined 24-bit values, and the one other
; partial write (`ld h,a`) is to an HL whose upper byte `add hl,bc` has
; just set (and which the shifts then discard).
; ===========================================================================
;;sect code __imul g
__imul:
        dec     sp
        dec     sp                      ; room so b2 can be the low byte of a pop
        push    hl                      ; S-5..S-3 = b0, b1, b2
        push    de                      ; S-8..S-6 = a0, a1, a2
        inc     sp
        inc     sp                      ; SP = S-6, at a2
        pop     bc                      ; C = a2, B = b0
        mlt     bc
        ld      a, c                    ; A = low(a2*b0)
        pop     bc                      ; C = b2; SP = S again
        ld      b, e                    ; B = a0
        mlt     bc
        add     a, c                    ; + low(a0*b2)
        ld      b, d
        ld      c, h
        mlt     bc
        add     a, c                    ; + low(a1*b1): A = S mod 256

        ld      b, e
        ld      c, h
        mlt     bc                      ; BC = a0*b1
        ld      h, d                    ; HL = (b2):a1:b0
        ld      d, l                    ; DE = (a2):b0:a0
        mlt     hl                      ; HL = a1*b0
        mlt     de                      ; DE = P00 = a0*b0, DEU = 0
        add     hl, bc                  ; HL = X (up to 17 bits, HLU = 0 or 1)
        add     a, h
        ld      h, a                    ; HL = X + 256*S, mod 2^16 in H:L
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl                  ; HL = (X + 256*S) << 8, mod 2^24
        add     hl, de                  ; + P00
        ret

; ===========================================================================
; __idivu: hl = de / hl  (unsigned, dividend = de, divisor = hl), and also
; de = de % hl. Returning the remainder in DE as well (a register the
; binary contract lets a helper clobber) is what lets __iremu, __idivs and
; __irems share this one routine; C code only ever sees HL.
;
; Restoring binary long division, one quotient bit per round, with every
; working value in registers: no bss cell, so the routine is reentrant and
; lib/agon/kbint.s has nothing of it to save. Two loops, chosen by the
; divisor:
;
;   narrow (divisor 1..127): the remainder fits in A. Each round shifts
;     the dividend (HL) left with `add hl,hl`, which carries its top bit
;     into the remainder by `rla`; `cp c` tries the divisor, and when it
;     fits, `sub c` takes it off and `inc l` sets the quotient bit the shift
;     has just cleared, so the quotient grows in HL as the dividend leaves
;     it. 2*remainder+1 <= 253 never overflows A. 7 cycles a round.
;   wide (divisor 128..0x7FFFFF): the remainder is HL, the divisor BC and
;     the dividend/quotient DE (shifted through HL by `ex de,hl`). `adc
;     hl,hl` takes the dividend bit into the remainder; the quotient bit is
;     set by `inc e` before the trial `sbc hl,bc` and undone with the
;     restoring `add hl,bc` when the divisor does not fit (INC leaves the
;     carry alone, and ADC left it clear: the remainder is below the
;     divisor, so below 2^23, and doubling it cannot carry out). 12 cycles.
;   A divisor of 0x800000 or more gives a quotient of 0 or 1: one compare.
;
; Fewer rounds for small operands: the rounds go 8 at a time (B counts the
; groups in the narrow loop, A in the wide one, where BC is the divisor),
; one group per significant byte of the dividend; the dividend is first
; shifted up by its leading zero bytes, which as rounds would only shift
; zeros into a zero remainder. In the narrow loop a top byte below the
; divisor is the remainder outright, saving its 8 rounds; in the wide loop
; the top S bytes are, where S is the divisor's byte length less one (the
; top S bytes are below 256^S <= divisor), and a dividend below the
; divisor returns at once with quotient 0.
;
; Division by zero (undefined in C) keeps the old result: quotient
; 0xFFFFFF, remainder the dividend.
;
; Interrupts: the wide setup reads bytes out of a register by pushing it
; and popping it back at an offset; it only ever reads at or above SP, so
; an interrupt's pushes below SP cannot disturb it.
; ===========================================================================
;;sect code __idivu g
__idivu:
        ld      bc, 0xFFFF80            ; -128
        add     hl, bc                  ; C iff divisor >= 128
        jp      c, @wide
        ; ---- narrow: divisor 0..127, HL = divisor - 128, L = divisor ^ 0x80
        ld      a, l
        xor     0x80                    ; A = divisor, Z iff divisor = 0
        jr      z, @dz
        ld      c, a                    ; C = divisor
        ex      de, hl                  ; HL = dividend
        ld      de, 0xFF0000
        add     hl, de                  ; C iff dividend >= 0x10000; low 16 kept
        jr      c, @n3
        ld      a, h
        or      a
        jr      z, @n1                  ; dividend < 0x100
        cp      c
        jr      c, @n2s                 ; H < divisor: H is the remainder
        ; two bytes: shift them to the top (HLU, garbage, goes out), 16 rounds
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        ld      b, 2
        xor     a
@nrounds:
        add     hl, hl                  ; 1
        rla
        cp      c
        jr      c, $+4
        sub     c
        inc     l
        add     hl, hl                  ; 2
        rla
        cp      c
        jr      c, $+4
        sub     c
        inc     l
        add     hl, hl                  ; 3
        rla
        cp      c
        jr      c, $+4
        sub     c
        inc     l
        add     hl, hl                  ; 4
        rla
        cp      c
        jr      c, $+4
        sub     c
        inc     l
        add     hl, hl                  ; 5
        rla
        cp      c
        jr      c, $+4
        sub     c
        inc     l
        add     hl, hl                  ; 6
        rla
        cp      c
        jr      c, $+4
        sub     c
        inc     l
        add     hl, hl                  ; 7
        rla
        cp      c
        jr      c, $+4
        sub     c
        inc     l
        add     hl, hl                  ; 8
        rla
        cp      c
        jr      c, $+4
        sub     c
        inc     l
        djnz    @nrounds
        ld      de, 0
        ld      e, a                    ; DE = remainder
        ret                             ; HL = quotient
@dz:    scf
        sbc     hl, hl                  ; HL = 0xFFFFFF, DE = dividend
        ret
@n1:    ld      a, l                    ; one byte (A was 0)
        cp      c
        jr      c, @tiny                ; dividend < divisor
        xor     a
@n2s:   ld      h, l                    ; A = remainder so far; L is the
        ld      l, 0                    ; last byte: to the top, 8 rounds
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        ld      b, 1
        jr      @nrounds
@n3:    or      a
        sbc     hl, de                  ; HL = dividend again
        ld      b, 3
        xor     a
        jr      @nrounds
@tiny:  ld      de, 0
        ld      e, a                    ; DE = remainder = dividend
        ld      hl, 0                   ; HL = quotient = 0
        ret

        ; ---- wide: HL = divisor - 128, DE = dividend. BC holds the
        ; divisor, so A counts the groups of 8 rounds here.
@wide:  ld      bc, 0xFF0080            ; 128 - 0x10000
        add     hl, bc                  ; HL = divisor - 0x10000, C iff >= it
        jr      c, @wbig
        ld      a, h                    ; divisor's H (the low 16 bits kept)
        ld      bc, 0x010000
        add     hl, bc                  ; HL = divisor
        push    hl
        pop     bc                      ; BC = divisor
        ex      de, hl                  ; HL = dividend
        or      a
        sbc     hl, bc
        jp      c, @wless               ; dividend < divisor
        add     hl, bc                  ; HL = dividend
        ld      de, 0xFF0000
        or      a
        jr      nz, @w1                 ; divisor >= 0x100: S = 1
        ; S = 0, divisor 0x80..0xFF: all the dividend's bytes take rounds
        add     hl, de                  ; C iff dividend >= 0x10000
        jr      c, @w0n3
        inc     h
        dec     h
        jr      z, @w0n1                ; dividend < 0x100
        add     hl, hl                  ; two bytes: to the top
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        ld      a, 2
        jr      @w0go
@w0n1:  ld      h, l                    ; one byte: to the top
        ld      l, 0
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        ld      a, 1
        jr      @w0go
@w0n3:  or      a
        sbc     hl, de                  ; HL = dividend again
        ld      a, 3
@w0go:  ld      de, 0                   ; remainder 0
        jr      @wready
        ; S = 1, divisor 0x100..0xFFFF, so the dividend has 2 or 3 bytes
        ; and its top one is the remainder outright
@w1:    add     hl, de                  ; C iff dividend >= 0x10000
        jr      c, @w1n3
        ld      de, 0
        ld      e, h                    ; DE = H, the top byte
        ld      h, l                    ; the low byte to the top
        ld      l, 0
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        ld      a, 1
        jr      @wready
@w1n3:  or      a
        sbc     hl, de                  ; HL = dividend again
        ld      de, 0
        push    de                      ; three zero bytes above a copy:
        push    hl                      ; L, H, HLU at SP, SP+1, SP+2
        inc     sp
        inc     sp
        pop     de                      ; DE = HLU, the top byte
        inc     sp
        add     hl, hl                  ; the other two to the top
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        ld      a, 2
        jr      @wready
        ; divisor >= 0x10000: HL = divisor - 0x10000
@wbig:  ld      bc, 0x810000            ; 0x10000 - 0x800000
        add     hl, bc                  ; HL = divisor - 0x800000, C iff >= it
        ld      bc, 0x800000
        jp      c, @huge
        add     hl, bc                  ; HL = divisor
        push    hl
        pop     bc                      ; BC = divisor
        ex      de, hl                  ; HL = dividend
        or      a
        sbc     hl, bc
        jp      c, @wless               ; dividend < divisor
        add     hl, bc                  ; HL = dividend, 3 bytes: S = 2,
        ld      de, 0                   ; its top two are the remainder
        push    de
        push    hl
        inc     sp
        pop     de                      ; DE = HL >> 8
        inc     sp
        inc     sp
        add     hl, hl                  ; the low byte to the top
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        ld      a, 1
@wready:                                ; DE = remainder, HL = dividend's rest,
        ex      de, hl                  ; A = groups: now HL = rem, DE = rest
@wrounds:
        ex      de, hl                  ; 1
        add     hl, hl
        ex      de, hl
        adc     hl, hl
        inc     e
        sbc     hl, bc
        jr      nc, $+4
        add     hl, bc
        dec     e
        ex      de, hl                  ; 2
        add     hl, hl
        ex      de, hl
        adc     hl, hl
        inc     e
        sbc     hl, bc
        jr      nc, $+4
        add     hl, bc
        dec     e
        ex      de, hl                  ; 3
        add     hl, hl
        ex      de, hl
        adc     hl, hl
        inc     e
        sbc     hl, bc
        jr      nc, $+4
        add     hl, bc
        dec     e
        ex      de, hl                  ; 4
        add     hl, hl
        ex      de, hl
        adc     hl, hl
        inc     e
        sbc     hl, bc
        jr      nc, $+4
        add     hl, bc
        dec     e
        ex      de, hl                  ; 5
        add     hl, hl
        ex      de, hl
        adc     hl, hl
        inc     e
        sbc     hl, bc
        jr      nc, $+4
        add     hl, bc
        dec     e
        ex      de, hl                  ; 6
        add     hl, hl
        ex      de, hl
        adc     hl, hl
        inc     e
        sbc     hl, bc
        jr      nc, $+4
        add     hl, bc
        dec     e
        ex      de, hl                  ; 7
        add     hl, hl
        ex      de, hl
        adc     hl, hl
        inc     e
        sbc     hl, bc
        jr      nc, $+4
        add     hl, bc
        dec     e
        ex      de, hl                  ; 8
        add     hl, hl
        ex      de, hl
        adc     hl, hl
        inc     e
        sbc     hl, bc
        jr      nc, $+4
        add     hl, bc
        dec     e
        dec     a
        jr      nz, @wrounds
        ex      de, hl                  ; HL = quotient, DE = remainder
        ret
@wless: add     hl, bc                  ; HL = dividend
        ex      de, hl                  ; DE = remainder = dividend
        ld      hl, 0                   ; quotient 0
        ret
@huge:  add     hl, bc                  ; HL = divisor (>= 0x800000)
        push    hl
        pop     bc                      ; BC = divisor
        ex      de, hl                  ; HL = dividend
        or      a
        sbc     hl, bc
        jr      nc, @hone               ; dividend >= divisor: quotient 1
        add     hl, bc
        ex      de, hl                  ; DE = remainder = dividend
        ld      hl, 0
        ret
@hone:  ex      de, hl                  ; DE = dividend - divisor
        ld      hl, 1
        ret

; ===========================================================================
; __iremu: hl = de % hl  (unsigned): __idivu leaves the remainder in DE.
; ===========================================================================
;;sect code __iremu g
;;ref __idivu
__iremu:
        call    __idivu
        ex      de, hl
        ret

; ===========================================================================
; __idivs: hl = de / hl  (signed, truncating toward zero, per C's /).
; Divides the magnitudes with __idivu and negates the quotient if exactly
; one operand was negative; the signs steer the branches, so nothing is
; kept in memory.
;
; Sign test: with BC = 0x800000, `add hl,bc` twice leaves HL as it was
; (adding 2^24 in all) and the second add carries exactly when HL is
; non-negative (the first flipped bit 23, and the second carries it out).
; Negating is `0 - x` by `sbc hl,hl` / `sbc hl,de`, as the eZ80 has no
; 24-bit NEG. The most negative int, -8388608, negates to itself, which as
; an unsigned magnitude (0x800000) is right; -8388608 / -1 wraps back to
; -8388608 (C leaves that overflow undefined).
; ===========================================================================
;;sect code __idivs g
;;ref __idivu
__idivs:
        ld      bc, 0x800000
        add     hl, bc
        add     hl, bc                  ; C iff divisor >= 0
        jr      nc, @dvrneg
        ex      de, hl
        add     hl, bc
        add     hl, bc                  ; C iff dividend >= 0
        ex      de, hl
        jp      c, __idivu              ; both >= 0: the quotient as it is
@negdvd:                                ; dividend < 0 <= divisor
        push    hl
        or      a
        sbc     hl, hl
        sbc     hl, de
        ex      de, hl                  ; DE = -dividend
        pop     hl
        call    __idivu
@negq:  ex      de, hl
        or      a
        sbc     hl, hl
        sbc     hl, de                  ; HL = -quotient
        ret
@dvrneg:                                ; divisor < 0
        push    de
        ex      de, hl
        or      a
        sbc     hl, hl
        sbc     hl, de                  ; HL = -divisor
        pop     de
        ex      de, hl
        add     hl, bc
        add     hl, bc                  ; C iff dividend >= 0
        ex      de, hl
        jr      nc, @bothneg
        call    __idivu                 ; signs differ: negate
        jr      @negq
@bothneg:
        push    hl
        or      a
        sbc     hl, hl
        sbc     hl, de
        ex      de, hl                  ; DE = -dividend
        pop     hl
        jp      __idivu                 ; signs agree: the quotient as it is

; ===========================================================================
; __irems: hl = de % hl  (signed; the result takes the DIVIDEND's sign, per
; C's %). The divisor's sign does not matter: its magnitude is used. Same
; sign tests and negation as __idivs; __idivu's remainder is in DE.
; ===========================================================================
;;sect code __irems g
;;ref __idivu
__irems:
        ld      bc, 0x800000
        add     hl, bc
        add     hl, bc                  ; C iff divisor >= 0
        jr      nc, @dvrneg
@dvrok: ex      de, hl
        add     hl, bc
        add     hl, bc                  ; C iff dividend >= 0
        ex      de, hl
        jr      nc, @dvdneg
        call    __idivu
        ex      de, hl                  ; HL = remainder
        ret
@dvdneg:
        push    hl
        or      a
        sbc     hl, hl
        sbc     hl, de
        ex      de, hl                  ; DE = -dividend
        pop     hl
        call    __idivu
        or      a
        sbc     hl, hl
        sbc     hl, de                  ; HL = -remainder
        ret
@dvrneg:
        push    de
        ex      de, hl
        or      a
        sbc     hl, hl
        sbc     hl, de                  ; HL = -divisor
        pop     de
        jr      @dvrok

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
; and unsigned). Built from MLT byte products, as __imul is: with the left
; operand's bytes a0..a3 and the right's b0..b3, byte k of the product
; gathers every a_i*b_j with i + j = k, and only the ten pairs with
; i + j <= 3 reach the low 32 bits. One column at a time: a 24-bit
; accumulator in HL takes the column's carry and its 16-bit products
; (MLT leaves BC's top byte zero, so `add hl,bc` adds exactly the
; product); its low byte is the result's byte k, and the rest, shifted
; down a byte, is the next column's carry. The shift is a store and a
; reload one byte up, over frame bytes kept zero for it. Column 3 needs
; only the low bytes. Scratch: the left operand = 0..3, the right = 4..7,
; the result and the carries = 8..13 (11..13 start zero).
;;sect code __lmul g
__lmul:
        ld      iy, -14
        add     iy, sp
        ld      sp, iy
        ld      (iy+0), bc
        ld      (iy+3), a
        ld      (iy+4), hl
        ld      (iy+7), e
        ld      hl, 0
        ld      (iy+11), hl             ; the zeros the reloads read
        ; column 0: a0*b0
        ld      b, (iy+0)
        ld      c, (iy+4)
        mlt     bc
        ld      (iy+8), bc              ; byte 0, and the carry at 9
        ld      hl, (iy+9)              ; HL = the carry (bytes 10, 11 are 0)
        ; column 1: a0*b1, a1*b0
        ld      b, (iy+0)
        ld      c, (iy+5)
        mlt     bc
        add     hl, bc
        ld      b, (iy+1)
        ld      c, (iy+4)
        mlt     bc
        add     hl, bc
        ld      (iy+9), hl              ; byte 1
        ld      hl, (iy+10)             ; the carry (byte 12 is 0)
        ; column 2: a0*b2, a1*b1, a2*b0
        ld      b, (iy+0)
        ld      c, (iy+6)
        mlt     bc
        add     hl, bc
        ld      b, (iy+1)
        ld      c, (iy+5)
        mlt     bc
        add     hl, bc
        ld      b, (iy+2)
        ld      c, (iy+4)
        mlt     bc
        add     hl, bc
        ld      (iy+10), hl             ; byte 2
        ld      hl, (iy+11)             ; the carry (byte 13 is 0)
        ; column 3: the low bytes of a0*b3, a1*b2, a2*b1, a3*b0
        ld      a, l
        ld      b, (iy+0)
        ld      c, (iy+7)
        mlt     bc
        add     a, c
        ld      b, (iy+1)
        ld      c, (iy+6)
        mlt     bc
        add     a, c
        ld      b, (iy+2)
        ld      c, (iy+5)
        mlt     bc
        add     a, c
        ld      b, (iy+3)
        ld      c, (iy+4)
        mlt     bc
        add     a, c
        ld      e, a                    ; byte 3
        ld      hl, (iy+8)              ; bytes 0..2
        ld      iy, 14
        add     iy, sp
        ld      sp, iy
        ret

; __ldivmodu: E:UHL = A:UBC / E:UHL and A:UBC = A:UBC % E:UHL, unsigned.
; Restoring division, one quotient bit per round, run only over the
; dividend bytes that can give a nonzero quotient byte, with the working
; values in registers. If the dividend has bn significant bytes and the
; divisor bd, the quotient has at most k = bn - bd + 1 bytes: the top
; bd - 1 bytes of the dividend go straight into the partial remainder R
; (they are below 256^(bd-1) <= the divisor, so their quotient bits are
; all 0) and only the low k bytes go through the rounds: 24 rounds for a
; 32-bit dividend and a 16-bit divisor, 16 for a 24-bit dividend, none
; when k <= 0 (the quotient is 0 and the remainder the dividend).
; The fast loop, for a divisor of at most 2^23: R in HL, the divisor in
; DE, the dividend byte in A. R < divisor <= 2^23, so 2R + 1 fits in 24
; bits; `sbc hl,de` is the trial subtraction and `add hl,de` undoes it
; when it borrows, which carries: the carry is the quotient bit inverted,
; and the next `rla` takes it into A as it moves the next dividend bit
; out (a ninth `rla` and a `cpl` give the quotient byte). The eight
; rounds of a byte are written out.
; The slow loop, for a divisor above 2^23 (so at most two quotient bytes,
; one if the divisor is 2^24 or more): R in A:UHL, 32 bits with a 33rd
; in the carry after the shift (a set 33rd bit means R exceeds the
; divisor), the divisor's low 24 bits in DE and its top byte in the
; frame; the quotient byte builds in C.
; Frame (IY = SP, 7 bytes): 0..3 the dividend, its bytes replaced by the
; quotient's from the top as they are made (bytes k..3 set to 0 first);
; 4..6 zero, so the 24-bit load at k reads R's starting value whatever k
; is (the slow loop keeps the divisor's top byte at 6, past any load).
; The quotient leaves through `pop hl` / `pop de`, which free the frame.
; Division by zero is undefined (it returns, with no particular result).
; Returns both results, so __ldivu, __lremu, __ldivs and __lrems all
; share this one routine. Clobbers A, F, BC, D and IY.
;;sect code __ldivmodu g
__ldivmodu:
        ld      iy, -7
        add     iy, sp
        ld      sp, iy                  ; IY = SP = the frame
        ld      (iy+0), bc
        ld      (iy+3), a               ; frame 0..3 = the dividend
        ex      de, hl                  ; DE = the divisor's low 24 bits
        ld      c, l                    ; C = its top byte
        ld      hl, 0
        ld      (iy+4), hl              ; frame 4..6 = 0
        or      a                       ; B = bn: test the dividend's
        ld      b, 4                    ; bytes from the top (ld leaves
        jr      nz, @bn                 ; the flags alone)
        ld      a, (iy+2)
        or      a
        ld      b, 3
        jr      nz, @bn
        ld      a, (iy+1)
        or      a
        ld      b, 2
        jr      nz, @bn
        ld      a, (iy+0)
        or      a
        ld      b, 1
        jr      z, @q0                  ; a dividend of 0
@bn:
        inc     c
        dec     c
        jr      nz, @big                ; divisor >= 2^24
        ld      hl, 0xFFFF
        or      a
        sbc     hl, de
        jr      c, @d3                  ; divisor >= 2^16
        inc     d
        dec     d
        jr      z, @k                   ; divisor < 2^8: k = bn
        dec     b                       ; a 2-byte divisor: k = bn - 1
        jr      nz, @k
@q0:                                    ; quotient 0, remainder the dividend
        ld      bc, (iy+0)
        ld      a, (iy+3)
        pop     de
        pop     de
        inc     sp
        ld      hl, 0
        ld      e, l
        ret
@d3:                                    ; divisor 2^16 .. 2^24 - 1: k = bn - 2
        ld      hl, 0x800000
        or      a
        sbc     hl, de                  ; C iff divisor > 2^23
        dec     b                       ; (dec leaves the carry alone)
        jr      z, @q0
        dec     b
        jr      z, @q0
        jr      nc, @k
        jr      @slow                   ; B = 1 or 2
@big:                                   ; divisor >= 2^24: k = 1 if bn = 4
        ld      a, b
        cp      4
        jr      nz, @q0
        ld      b, 1
@slow:                                  ; B = k; C = the divisor's top byte
        ld      (iy+6), c
        ld      a, b
        ld      bc, 0
        dec     a
        jr      z, @s1
        ld      hl, (iy+2)              ; k = 2: R = bytes 2..3
        ld      (iy+2), bc              ; quotient bytes 2..3 = 0 (and 4)
        xor     a                       ; R's top byte
        ld      c, (iy+1)
        call    @sbyte
        ld      (iy+1), c
        jr      @s0
@s1:    ld      hl, (iy+1)              ; k = 1: R = bytes 1..3
        ld      (iy+1), bc              ; quotient bytes 1..3 = 0
        xor     a
@s0:    ld      c, (iy+0)
        call    @sbyte
        ld      (iy+0), c
        push    hl
        pop     bc                      ; the remainder, A:UBC
        pop     hl
        pop     de
        inc     sp
        ret
@k:                                     ; B = k, 1 to 4
        ld      a, b
        ld      bc, 0
        ld      c, a
        add     iy, bc                  ; IY = frame + k
        ld      hl, (iy+0)              ; R = the dividend >> 8k
        ld      c, b                    ; BC = 0
        ld      (iy+0), bc              ; quotient bytes k..3 = 0
        ld      b, a                    ; B counts the bytes left
@byte:
        dec     iy
        ld      a, (iy+0)               ; the next dividend byte down
        rla                             ; next dividend bit out, ~q in
        adc     hl, hl                  ; R = 2R + bit, no carry out
        sbc     hl, de                  ; trial R - divisor
        jr      nc, @f1                 ; it fits: q = 1, carry clear
        add     hl, de                  ; undo it; this carries: q = 0
@f1:
        rla                             ; next dividend bit out, ~q in
        adc     hl, hl                  ; R = 2R + bit, no carry out
        sbc     hl, de                  ; trial R - divisor
        jr      nc, @f2                 ; it fits: q = 1, carry clear
        add     hl, de                  ; undo it; this carries: q = 0
@f2:
        rla                             ; next dividend bit out, ~q in
        adc     hl, hl                  ; R = 2R + bit, no carry out
        sbc     hl, de                  ; trial R - divisor
        jr      nc, @f3                 ; it fits: q = 1, carry clear
        add     hl, de                  ; undo it; this carries: q = 0
@f3:
        rla                             ; next dividend bit out, ~q in
        adc     hl, hl                  ; R = 2R + bit, no carry out
        sbc     hl, de                  ; trial R - divisor
        jr      nc, @f4                 ; it fits: q = 1, carry clear
        add     hl, de                  ; undo it; this carries: q = 0
@f4:
        rla                             ; next dividend bit out, ~q in
        adc     hl, hl                  ; R = 2R + bit, no carry out
        sbc     hl, de                  ; trial R - divisor
        jr      nc, @f5                 ; it fits: q = 1, carry clear
        add     hl, de                  ; undo it; this carries: q = 0
@f5:
        rla                             ; next dividend bit out, ~q in
        adc     hl, hl                  ; R = 2R + bit, no carry out
        sbc     hl, de                  ; trial R - divisor
        jr      nc, @f6                 ; it fits: q = 1, carry clear
        add     hl, de                  ; undo it; this carries: q = 0
@f6:
        rla                             ; next dividend bit out, ~q in
        adc     hl, hl                  ; R = 2R + bit, no carry out
        sbc     hl, de                  ; trial R - divisor
        jr      nc, @f7                 ; it fits: q = 1, carry clear
        add     hl, de                  ; undo it; this carries: q = 0
@f7:
        rla                             ; next dividend bit out, ~q in
        adc     hl, hl                  ; R = 2R + bit, no carry out
        sbc     hl, de                  ; trial R - divisor
        jr      nc, @f8                 ; it fits: q = 1, carry clear
        add     hl, de                  ; undo it; this carries: q = 0
@f8:
        rla                             ; the last inverted bit in
        cpl
        ld      (iy+0), a               ; the quotient byte
        djnz    @byte
        push    hl
        pop     bc                      ; the remainder, below 2^23
        xor     a
        pop     hl                      ; quotient bytes 0..2
        pop     de                      ; E = quotient byte 3
        inc     sp                      ; the frame's last byte
        ret

; Eight rounds of the slow loop: C = the dividend byte in, its quotient
; byte out; R = A:UHL; the divisor = (iy+6):UDE. Clobbers B.
@sbyte:
        ld      b, 8
@sr:    sla     c                       ; dividend bit out, q = 0 in
        adc     hl, hl
        adc     a, a                    ; R = 2R + bit; C = its 33rd bit
        jr      c, @sovf
        sbc     hl, de
        sbc     a, (iy+6)               ; trial R - divisor
        jr      c, @srest
        inc     c                       ; it fits: q = 1
        djnz    @sr
        ret
@srest: add     hl, de                  ; it does not: undo it
        adc     a, (iy+6)
        djnz    @sr
        ret
@sovf:  or      a                       ; R >= 2^32 > divisor: subtract,
        sbc     hl, de                  ; the result fits in 32 bits
        sbc     a, (iy+6)
        inc     c
        djnz    @sr
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
