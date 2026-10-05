; crt0 test: main clobbers IY, then returns 0. Run via autoexec ("t_iy"
; then "quit"): the run completing cleanly and quit being reached confirms
; nothing about IY-clobbering user code crashes crt0's own IY save/restore
; (crt0 pushes IY in step 1 and pops it in __exit, per abi.md section 3,
; defensively alongside MOS's own IY preservation around the whole call).

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

        ld      iy, 0x123456            ; clobber IY freely - crt0's
                                         ; contract makes it ordinary
                                         ; scratch for generated code

        ld      hl, 0
        ld      sp, ix
        pop     ix
        ret
