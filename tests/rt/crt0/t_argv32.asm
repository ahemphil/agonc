; crt0 test: invoked with 40 single/double-digit numeric tokens ("1" "2"
; ... "40"). Expect the 32-entry cap: argc==32 (argv[0] plus 31 recorded
; tokens), argv[1]=="1", argv[31]=="31" (tokens 32..40 silently dropped).
; Exits via port 0: 0 = pass, 1 = fail.

        assume  adl=1
        org     0x040000

        jp      $+0x45
        align   64
        db      "MOS", 0, 1

__bss_size:  equ     96
__bss_base:  equ     0x0B0000-__bss_size
__stack_top: equ     __bss_base
__argv:      equ     __bss_base+0

___int_restore: equ 0               ; crt0's weak reference, as ld resolves it unused

        include "lib\rt\crt0.s"

; exit(status), as the library's exit.c does it without atexit functions or
; streams: main's return reaches __exit through it (crt0 step 6).
_exit:
        pop     hl                      ; the return address
        pop     hl                      ; the status
        jp      __exit

_main:
        push    ix
        ld      ix, 0
        add     ix, sp

        ld      hl, (ix+6)              ; argc
        ld      de, 32
        or      a
        sbc     hl, de
        jr      nz, @fail

        ld      hl, (ix+9)
        ld      de, 3
        add     hl, de
        ld      hl, (hl)                ; argv[1]
        ld      de, str_1
        call    @streq
        jr      nz, @fail

        ld      hl, (ix+9)
        ld      de, 31*3
        add     hl, de
        ld      hl, (hl)                ; argv[31]
        ld      de, str_31
        call    @streq
        jr      nz, @fail

        ld      hl, msg_pass
        call    @printstr
        ld      sp, ix
        pop     ix
        xor     a
        out0    (0x00), a
        ld      hl, 0
        ret

@fail:
        ld      hl, msg_fail
        call    @printstr
        ld      sp, ix
        pop     ix
        ld      a, 1
        out0    (0x00), a
        ld      hl, 1
        ret

@printstr:
        push    bc
        push    af
        push    hl
        ld      bc, 0
        xor     a
        rst.lis 18h
        pop     hl
        pop     af
        pop     bc
        ret

@streq:
        ld      a, (de)
        cp      (hl)
        jr      nz, @streq_ret
        or      a
        jr      z, @streq_ret
        inc     hl
        inc     de
        jr      @streq
@streq_ret:
        ret

str_1:          db      "1", 0
str_31:         db      "31", 0
msg_pass:       db      "argv32 PASS", 13, 10, 0
msg_fail:       db      "argv32 FAIL", 13, 10, 0
