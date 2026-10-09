;;agonc-object 1
;;unit kbint.s
; ---------------------------------------------------------------------------
; kbint.s - the entry points MOS and the hardware call for a C keyboard
; or interrupt handler (handler.c, <agon/mos.h>). In libagon.s.
;
; MOS calls a keyboard vector from inside its UART0 interrupt with DE
; pointing at the key packet, having saved only AF, BC, DE and HL; the
; hardware enters an interrupt vector with nothing saved at all. A C
; function expects neither: it may change IY (and A, F, BC, DE, HL, and
; the alternate registers, which int64.c's long long division uses), and
; the runtime helpers it calls (shifts, sign extensions) keep their
; working values in fixed cells, which an interrupted helper in the main
; program is still using. So each entry here saves what MOS or the
; hardware does not, and __handler_call saves and restores AF', BC', DE',
; HL' and every rt.s scratch cell around the C function. (MOS's own
; interrupt handlers, vblank, UART0 and I2C, never touch the alternate
; registers, in MOS 2.3.3 and 3.0.2 alike.)
;
; The keyboard entry runs the library's Ctrl-C check first (crt0.s's
; __kbhandler), so a key handler never costs the program its Ctrl-C.
; There are four interrupt slots, each calling through its own pointer.
;
; This is a library unit in agonc's own object format (object_format.md):
; the lines starting with two semicolons are directives to ld, naming
; each section, the symbols it defines and the ones it refers to, so ld
; keeps these sections only in a program that installs a handler. C's
; _kb_fn and _int_fn (handler.c) are __kb_fn and __int_fn here (abi.md
; section 7); each _int_fn element is a 3-byte pointer, so slot s reads
; __int_fn+3*s.
;
; Map: __kb_entry (the keyboard vector), __handler_call (the shared call
; into C), __int_entry0-3 (the interrupt vectors).
; ---------------------------------------------------------------------------

;;sect code __kb_entry g
;;ref __kbhandler
;;ref __kb_fn
;;ref __handler_call
; MOS's keyboard vector while a C key handler is installed. DE is saved
; across __kbhandler, which steps DE through the packet, so the C
; function gets the packet's start. MOS has saved AF, BC, DE and HL; IX
; and IY are saved here, IY because __handler_call jumps through it and
; the C function may change it.
__kb_entry:
        push    de
        call    __kbhandler             ; Ctrl-C first, as crt0's own vector does
        pop     de
        push    ix
        push    iy
        ld      hl, (__kb_fn)
        call    __handler_call          ; fn(packet)
        pop     iy
        pop     ix
        ret

; Calls the C function at HL with DE as its argument, saving around it
; the runtime helpers' scratch cells (12 bytes in 4 pushes) and the
; alternate registers AF', BC', DE' and HL' (12 bytes in 4 pushes), which
; int64.c's long long division keeps working values in: neither MOS nor
; the hardware saves them, and the C function may divide.
; Clobbers everything else but IX (the C function keeps it) and SP.
;
; The cells are every bss cell rt.s has: the 3-byte ones of the shifts and
; the sign extensions. The restore pops them in the reverse order.
; (__imul and the divide helpers have no cells: they work in registers and
; on the stack, so an interrupted multiply or divide needs nothing saved.)
;
; The call itself follows the C convention (abi.md section 4): the
; argument pushed as one 3-byte slot, the return address above it, and
; the caller pops the argument afterwards. There is no indirect CALL
; instruction, so the return address (@back) is pushed by hand and the
; function entered with jp (iy); HL is needed for the cells, so the
; function's address waits in IY.
;;sect code __handler_call g
;;ref __ishru_buf
;;ref __ishrs_buf
;;ref __sext8_buf
;;ref __sext16_buf
__handler_call:
        push    hl
        pop     iy                      ; iy = the function
        ex      af, af'
        push    af
        ex      af, af'
        exx
        push    bc
        push    de
        push    hl
        exx
        ld      hl, (__ishru_buf)
        push    hl
        ld      hl, (__ishrs_buf)
        push    hl
        ld      hl, (__sext8_buf)
        push    hl
        ld      hl, (__sext16_buf)
        push    hl
        push    de                      ; the argument
        ld      hl, @back
        push    hl                      ; the return address
        jp      (iy)
@back:
        pop     de                      ; the argument back off
        pop     hl
        ld      (__sext16_buf), hl
        pop     hl
        ld      (__sext8_buf), hl
        pop     hl
        ld      (__ishrs_buf), hl
        pop     hl
        ld      (__ishru_buf), hl
        exx
        pop     hl
        pop     de
        pop     bc
        exx
        ex      af, af'
        pop     af
        ex      af, af'
        ret

; The interrupt slots: everything saved, the C function called, and the
; interrupt ended as MOS's own handlers end theirs. The eZ80 disables
; interrupts when it takes one, so EI comes just before the return;
; interrupts are enabled only after the instruction that follows EI, so
; no second interrupt can arrive before RETI.L has returned (the .L form
; is the one MOS's own handlers use). The four entries differ only in
; which _int_fn element they call.
;;sect code __int_entry0 g
;;ref __int_fn
;;ref __handler_call
__int_entry0:
        push    af
        push    bc
        push    de
        push    hl
        push    ix
        push    iy
        ld      hl, (__int_fn)
        call    __handler_call
        pop     iy
        pop     ix
        pop     hl
        pop     de
        pop     bc
        pop     af
        ei
        reti.l

;;sect code __int_entry1 g
;;ref __int_fn
;;ref __handler_call
__int_entry1:
        push    af
        push    bc
        push    de
        push    hl
        push    ix
        push    iy
        ld      hl, (__int_fn+3)
        call    __handler_call
        pop     iy
        pop     ix
        pop     hl
        pop     de
        pop     bc
        pop     af
        ei
        reti.l

;;sect code __int_entry2 g
;;ref __int_fn
;;ref __handler_call
__int_entry2:
        push    af
        push    bc
        push    de
        push    hl
        push    ix
        push    iy
        ld      hl, (__int_fn+6)
        call    __handler_call
        pop     iy
        pop     ix
        pop     hl
        pop     de
        pop     bc
        pop     af
        ei
        reti.l

;;sect code __int_entry3 g
;;ref __int_fn
;;ref __handler_call
__int_entry3:
        push    af
        push    bc
        push    de
        push    hl
        push    ix
        push    iy
        ld      hl, (__int_fn+9)
        call    __handler_call
        pop     iy
        pop     ix
        pop     hl
        pop     de
        pop     bc
        pop     af
        ei
        reti.l
;;end
