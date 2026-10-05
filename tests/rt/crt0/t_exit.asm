; crt0 test: main calls a() which calls b() which does `ld hl,N / jp __exit`
; directly, with several extra values left pushed on the stack (as a real
; exit() deep in a call chain would leave it - exit() never returns, so it
; never unwinds those frames itself). __exit must still work correctly:
; reload SP from __exit_sp (not by unwinding) and hand HL back to MOS.
;
; Run via autoexec ("t_exit" then "quit"): quit must be reached, proving
; a HL=0 exit through this path returns cleanly to MOS. A second program,
; t_exit21, does the same via `jp __exit` with HL=21 and is run TYPED, to
; confirm MOS's status-21 message still appears after an exit() from deep
; in the stack (not just from main's own `ret`, as t_ret21 already showed).

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
        ;;ref __exit                      ; (comment only: crt0.s already
                                            ; declares __exit as a label
                                            ; inside __start's own section)

_main:
        push    ix
        ld      ix, 0
        add     ix, sp
        call    fn_a
        ; unreachable: fn_a -> fn_b exits directly via __exit and never
        ; returns here.
        ld      hl, 99
        ld      sp, ix
        pop     ix
        ret

fn_a:
        push    hl                      ; leave junk on the stack, as real
        push    de                      ; code between main and exit() would
        call    fn_b
        ; unreachable
        pop     de
        pop     hl
        ret

fn_b:
        push    bc                      ; more junk left unpopped
        ld      hl, 0
        jp      __exit
