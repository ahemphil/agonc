;;agonc-object 1
;;unit crt0.s ce37
; ---------------------------------------------------------------------------
; crt0.s - C runtime startup for Agon C.
;
; The first code of every program: MOS jumps here, crt0 sets up the C
; world (a stack, zeroed bss, argc/argv, the Ctrl-C handler), calls main,
; and hands main's status back to MOS. ld places this file's __start first
; in the image it builds from cc2's units and the libraries (docs/abi.md
; section 8).
;
; Implements docs/abi.md section 9 exactly. Entered by MOS (or by the
; agonc driver moslet, identically) with:
;   A  = MB (0 for an ADL program)
;   DE(U) = the program's own execution address
;   HL(U) = pointer to a NUL-terminated argument string (arguments only,
;           no program name)
;   SP    = MOS's own small (2 KB) system stack
;   ADL mode on, interrupts enabled
;
; On return (via __exit, which exit() jumps to, also after main returns),
; HL holds the exit status and SP/IY are restored exactly as MOS left
; them, per the MOS program calling convention.
;
; Flow: step 1 saves MOS's SP and IY, step 2 moves to the program's own
; stack (just below bss), step 3 zeroes bss, step 3a records the start
; time for clock(), step 4 splits the argument string into argv[], step 5
; calls main, step 6 passes its result to exit(), and step 7 (__exit) is
; where every way out of the program ends. After them: __kb_on and
; __kbhandler (the Ctrl-C hook) and the data cells.
;
; Register-invariant discipline (docs/abi.md section 3): every place this
; file builds a 24-bit value from an 8-bit source starts with a full
; `ld hl,0` (or `ld bc,0` etc.) so the otherwise-inaccessible upper byte
; (HLU/BCU/...) is never left stale. A stale upper byte is a silent,
; data-dependent fault, so it is called out at each occurrence below.
;
; Lines starting `;;` are directives to ld, not comments
; (docs/object_format.md): `;;sect` opens a section, `;;ref` names a symbol
; the section uses (so ld keeps the section defining it), `;;wref` makes
; this unit's references to a symbol weak (it is 0 unless something else
; brings it in), `;;label` declares a further global label in a section.
;
; SECTION ORDER IN THIS FILE MATTERS (docs/object_format.md): `ld` places
; every selected CODE section before any DATA section, so that user code's
; forward-only-in-the-source-text but backward-in-the-final-image
; convention holds, and so `__start` - placed first among code sections -
; lands at exactly file offset 0x45 for the header's `jp $+0x45` entry
; jump. __start's code section is therefore written FIRST in this file,
; before the data cells it references. With the data first, three
; zero-initialised data cells (7 bytes, which assemble as NOP) would sit
; ahead of __start, and the entry jump would reach working code only by
; the accident of NOP-encoded zero bytes - not something to rely on.
; ---------------------------------------------------------------------------

;;sect code __start g
;;ref _main
;;ref _exit
;;ref ___kb_on
;;ref ___clock0
;;ref __exit_sp
;;ref __argstr
;;ref __argv
;;ref __argv0
;;ref __stack_top
;;ref __bss_base
;;ref __bss_size
;;label __exit
;;wref ___int_restore
;;ref ___int_restore
__start:
        ; ---- step 1: save MOS's SP, save the incoming args pointer -------
        push    iy                      ; IY is scratch for generated code
                                         ; but crt0 preserves it defensively
                                         ; around main, per abi.md section 3
        ld      (__exit_sp), sp         ; __exit restores SP from here
        ld      (__argstr), hl          ; stash args ptr: HL is about to be
                                         ; clobbered by the bss-zero step

        ; ---- step 2: switch to our own stack -------------------------------
        ld      sp, __stack_top

        ; ---- step 3: zero __bss_size bytes at __bss_base, skip if zero ----
        ld      bc, __bss_size          ; a full 24-bit immediate load, so
                                         ; BCU holds the size's top byte
        ; The zero tests below are needed because LDIR with BC = 0 does
        ; not copy nothing: it decrements first and repeats until BC is 0,
        ; so it would run 16M times. Each tests all 24 bits of BC (0 - BC
        ; is zero only when BC is), since "ld a,b / or c" would take a
        ; size that is a multiple of 64K for 0.
        ld      hl, 0
        or      a
        sbc     hl, bc                  ; Z iff bss_size = 0
        jr      z, @nobss
        ld      hl, __bss_base
        ld      (hl), 0                 ; first byte
        dec     bc                      ; bc = bss_size-1 remaining
        push    hl
        ld      hl, 0
        or      a
        sbc     hl, bc                  ; Z iff bss_size was 1: nothing left
        pop     hl                      ; (pop leaves the flags alone)
        jr      z, @nobss
        push    hl
        pop     de                      ; de = bss_base (full 24-bit copy;
                                         ; there is no ld de,hl on eZ80)
        inc     de                      ; de = bss_base+1
        ldir                            ; classic self-propagating zero
                                         ; fill: each byte copies the zero
                                         ; just written one position behind
@nobss:

        ; ---- step 3a: MOS's timer now, for clock() (M12) --------------------
        ; mos_sysvars returns the address of MOS's system variables in IX;
        ; IX is free to change here, as step 4 reloads it.
        ld      a, 0x08                 ; mos_sysvars: IX = the block
        rst.lis 08h
        ld      hl, (ix+0)              ; sysvar_time: 4 bytes of centiseconds
        ld      (___clock0), hl
        ld      a, (ix+3)
        ld      (___clock0+3), a

        ; ---- step 4: split the argument string in place into __argv[] ----
        ; argv[0] is always the fixed empty string; further entries are
        ; whitespace-separated tokens of the incoming string, NUL-written
        ; over their trailing separator. A token that starts with a double
        ; quote runs to the next one, spaces and all, and both quotes are
        ; dropped (MOS 3 quotes file names containing spaces). At most 32
        ; entries total (including argv[0]); further tokens are dropped
        ; once the table is full - per abi.md/driver.md.
        ; Registers through the loop: B = argc so far, IX = the address of
        ; the next free argv slot, HL = the cursor in the string. The
        ; entries after argv[argc - 1] read as null because step 3 zeroed
        ; the table, whose 33rd slot is never filled: argv[argc] is null
        ; even when all 32 are used, as C requires.
        ld      hl, __argv0
        ld      (__argv), hl
        ld      b, 1                    ; argc so far (argv[0] filled);
                                         ; used only as a plain byte counter
                                         ; until the final zero-extend below
        ld      ix, __argv+3            ; slot for the next token (argv[1])
        ld      hl, (__argstr)          ; hl = cursor into the arg string
        ; skip the separators before a token
@skipws:
        ld      a, (hl)
        or      a
        jr      z, @argvdone            ; end of string
        cp      ' '
        jr      z, @isws
        cp      9                       ; tab
        jr      nz, @havetok
@isws:
        inc     hl
        jr      @skipws
@havetok:
        ld      c, a                    ; the token's first character
        ld      a, b
        cp      32
        jr      nc, @argvdone           ; table already full: stop (further
                                         ; tokens are simply dropped, per spec)
        ld      a, c
        cp      34                      ; '"': a quoted token
        jr      nz, @plain
        inc     hl                      ; it starts after its quote
        ld      (ix+0), hl              ; record it in the next argv slot
        inc     b
        ld      de, 3
        add     ix, de
@quoted:
        ld      a, (hl)
        or      a
        jr      z, @argvdone            ; no closing quote: it runs to the end
        cp      34
        jr      z, @endtok              ; the closing quote becomes its NUL
        inc     hl
        jr      @quoted
@plain:
        ld      (ix+0), hl              ; record this token's address (a
                                         ; full 24-bit store, no partial-
                                         ; load hazard on the store side)
        inc     b
        ld      de, 3
        add     ix, de                  ; advance to the next argv slot
@consume:
        ld      a, (hl)
        or      a
        jr      z, @argvdone            ; end of string; this token's NUL
                                         ; terminator is already in place
        cp      ' '
        jr      z, @endtok
        cp      9
        jr      z, @endtok
        inc     hl
        jr      @consume
@endtok:
        ld      (hl), 0                 ; NUL-terminate this token
        inc     hl
        jr      @skipws
@argvdone:
        ld      hl, 0                   ; full 24-bit zero FIRST, so HLU is
        ld      l, b                    ; correctly 0, then overwrite just
                                         ; the low byte with argc (max 32,
                                         ; fits) - the canonical-value-safe
                                         ; zero-extend idiom from abi.md
                                         ; section 3

        ; ---- step 5: call main(argc, argv), Ctrl-C's handler installed -----
        push    hl                      ; argc: ___kb_on uses HL
        call    ___kb_on                ; (M12; removed again in __exit)
        pop     hl
        ex      de, hl                  ; de = argc (full 24-bit swap)
        ld      hl, __argv
        push    hl                      ; push argv (rightmost parameter,
                                         ; pushed first per abi.md section 4)
        push    de                      ; push argc (leftmost parameter,
                                         ; pushed last -> ends up at ix+6)
        call    _main
        pop     de                      ; caller removes the arguments
        pop     de                      ; (abi.md section 4); de is scratch,
                                         ; both slots are simply discarded

        ; ---- step 6: main's return is exit(status) (C89 2.1.2.2.3) -------
        ; exit runs the atexit functions and closes the streams, then
        ; jumps to __exit; it never returns here.
        push    hl                      ; exit's one argument: the status
        call    _exit

        ; ---- step 7: __exit - HL holds the status ---------------------------
        ; Reached by a `jp __exit` from exit() or a signal's default action
        ; (docs/abi.md section 9), arbitrarily deep in the call stack and
        ; with the stack otherwise unbalanced: this still works correctly
        ; because SP is reloaded from __exit_sp rather than unwound.
__exit:
        push    hl                      ; the status, across the calls below
        ; interrupt vectors a C handler took (handler.c) go back as they
        ; were; a weak reference, 0 unless the program installed one.
        ; `or a` / `sbc hl,de` with DE = 0 is the 24-bit test of HL for
        ; zero (there is no 24-bit compare instruction).
        ld      hl, ___int_restore
        ld      de, 0
        or      a
        sbc     hl, de
        call    nz, ___int_restore
        ld      hl, 0                   ; MOS must not call the Ctrl-C handler
        ld      c, 0                    ; once the program is gone
        ld      a, 0x1D                 ; mos_setkbvector
        rst.lis 08h
        pop     hl
        ld      sp, (__exit_sp)         ; MOS's stack, as step 1 left it
        pop     iy
        ret                             ; to MOS, HL = the status

;;sect code ___kb_on g
;;ref __kbhandler
; void __kb_on(void): installs the Ctrl-C handler as MOS's keyboard vector
; (M12). crt0 calls it at startup; the driver calls it again after each
; program it runs, since that program's __exit removed it.
___kb_on:
        ld      hl, __kbhandler
        ld      c, 0                    ; a 24-bit address
        ld      a, 0x1D                 ; mos_setkbvector
        rst.lis 08h
        ret

;;sect code __kbhandler g
;;ref ___intflag
; Called by MOS 2.3.3 and 3.x from the UART0 interrupt on every key event,
; with DE pointing at the VDP's packet (ASCII code, modifiers, virtual key,
; 1 if pressed); MOS's own key handling follows. It only notes a Ctrl-C
; press: the library acts on it at its next I/O or MOS call (exit.c's
; __interrupted; docs/c89_spec.md section 15). Uses only A, F and DE: it
; runs inside MOS's interrupt, which saves only AF, BC, DE and HL around
; it (lib/agon/kbint.s). The flag gets the packet's pressed byte, which
; is non-zero.
__kbhandler:
        ld      a, (de)
        cp      3                       ; Ctrl-C's ASCII code
        ret     nz
        inc     de
        inc     de
        inc     de
        ld      a, (de)                 ; packet byte 3: pressed or released
        or      a
        ret     z                       ; its release
        ld      (___intflag), a
        ret

;;sect data __exit_sp g
; Saved copy of MOS's own SP, captured before we switch to our own stack.
; Must be a DATA object, not bss: it is written in step 1, and step 3
; zeroes the whole bss region, which would wipe it if it lived there.
__exit_sp:
        dl      0

;;sect data __argstr g
; Scratch cell holding the incoming argument-string pointer across the
; bss-zeroing step, which necessarily clobbers HL (LDIR's source register).
; Same reasoning as __exit_sp: must not be bss.
__argstr:
        dl      0

;;sect data ___clock0 g
; C's __clock0: MOS's timer (sysvar_time, centiseconds) when the program
; started, which clock() counts from (time.c).
___clock0:
        db      0, 0, 0, 0

;;sect data ___intflag g
; C's __intflag: non-zero once Ctrl-C has been pressed and not yet acted on.
; Data rather than bss, like the cells above, so the crt0 tests, which
; include this file with no bss of their own, have it too.
___intflag:
        db      0

;;sect data __argv0 g
; The fixed empty string used as argv[0] (MOS gives a program no name of
; its own to report).
__argv0:
        db      0

;;sect bss __argv g 99
; argv[] table: up to 32 entries (MAXARGV per abi.md/driver.md), 3 bytes
; (one address) each, and a 33rd that stays null, so argv[argc] is null
; however many are used. Zeroed by step 3 before step 4 fills in the
; entries actually used.
; (No body: a bss section is declared by its marker alone.)

;;end
