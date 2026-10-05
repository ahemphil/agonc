; crt0 test: main() returns 21. Run via autoexec as "t_ret21" then "quit":
; because MOS treats any non-zero status as an error (it prints "Invalid
; executable" for 21 specifically, though the printed text is not what
; this test checks: the emulator's transcript is not reliable enough), the
; script must halt right here and "quit" must NEVER run. test_rt.py checks
; for this via the harness's exit code 125 ("nothing ever wrote port 0"), which is
; what proves main's return value in HL really reached MOS as a non-zero
; status - a robust signal, unlike scraping the transcript for MOS's text.

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

        ld      hl, 21

        ld      sp, ix
        pop     ix
        ret
