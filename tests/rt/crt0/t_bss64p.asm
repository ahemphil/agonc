; crt0 test: __bss_size = 0x10001, 64K + 1. Run chained after t_dirty64
; (which fills the region with 0xA5) via "--cmd t_dirty64 --cmd t_bss64p":
; every byte after __argv's 96 must read back as zero, proving step 3's
; zero tests use all 24 bits of the size (testing B and C alone takes a
; multiple of 64K for 0, and after the first byte 64K + 1 for 1).
; Exits via port 0: 0 = pass, 1 = fail.

        assume  adl=1
        org     0x040000

        jp      $+0x45
        align   64
        db      "MOS", 0, 1

__bss_size:   equ     0x10001
__bss_base:   equ     0x0B0000-__bss_size
__stack_top:  equ     __bss_base
__argv:       equ     __bss_base+0
t_bss_area:   equ     __bss_base+96
T_BSS_AREA_END: equ  __bss_base+__bss_size

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

        ld      hl, t_bss_area
        ld      de, T_BSS_AREA_END
@loop:
        ld      a, (hl)
        or      a
        jr      nz, @fail
        inc     hl
        or      a
        sbc     hl, de
        add     hl, de                  ; hl back, with Z iff it reached the end
        jr      nz, @loop
@alldone:
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

msg_pass:       db      "t_bss64p PASS", 13, 10, 0
msg_fail:       db      "t_bss64p FAIL", 13, 10, 0
