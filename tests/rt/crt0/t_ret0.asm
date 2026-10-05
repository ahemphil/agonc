; crt0 test: main() returns 0. Run via autoexec ("t_ret0" then "quit"):
; quit must be reached, proving a zero exit status lets MOS's script
; continuation proceed to the next line.
;
; This file plays the part of `ld`'s output for a minimal one-function
; program: header, the bss/stack equates `ld` would compute, then crt0's
; body (included - crt0.s's ";;" markers are plain ez80asm comments), then
; this test's own _main.

        assume  adl=1
        org     0x040000

        jp      $+0x45
        align   64
        db      "MOS", 0, 1

__bss_size:  equ     96              ; just the __argv table
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

        ld      hl, 0

        ld      sp, ix
        pop     ix
        ret
