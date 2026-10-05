; crt0 test: identical shape to t_exit.asm, but fn_b exits with HL=21 via
; `jp __exit`. Run via autoexec as "t_exit21" then "quit": as with
; t_ret21.asm, the non-zero status must halt the script before "quit" can
; run (checked via the harness's exit code 125), confirming exit() from
; deep in the call stack propagates its status exactly as a plain `ret`
; from main does.

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
        call    fn_a
        ld      hl, 99
        ld      sp, ix
        pop     ix
        ret

fn_a:
        push    hl
        push    de
        call    fn_b
        pop     de
        pop     hl
        ret

fn_b:
        push    bc
        ld      hl, 21
        jp      __exit
