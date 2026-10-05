; crt0 test: on entry to main, SP must be exactly __stack_top - 9 (crt0
; pushes argv, argc, then `call _main` pushes the return address: 3 slots
; of 3 bytes each). Exits via port 0: 0 = pass, 1 = fail.

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
        ld      hl, 0
        add     hl, sp                  ; hl = sp, on entry, before any of
                                         ; main's own prologue runs
        ld      de, __stack_top-9
        or      a
        sbc     hl, de
        jr      nz, @fail

        push    ix
        ld      ix, 0
        add     ix, sp

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

msg_pass:       db      "stack PASS", 13, 10, 0
msg_fail:       db      "stack FAIL", 13, 10, 0
