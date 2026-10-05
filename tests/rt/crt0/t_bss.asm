; crt0 test: __bss_size = 608 (96 bytes for __argv, plus a 512-byte test
; area right after it). Run chained after t_dirty (which fills the whole
; 608-byte region with 0xA5) via "--cmd t_dirty --cmd t_bss": this program
; checks that every byte of the 512-byte test area - deliberately NOT
; __argv itself, which crt0 legitimately writes into during step 4 - reads
; back as zero, proving step 3's bss-zero loop really ran and covered the
; whole __bss_size, not just __argv.  Exits via port 0: 0 = pass, 1 = fail.

        assume  adl=1
        org     0x040000

        jp      $+0x45
        align   64
        db      "MOS", 0, 1

__bss_size:   equ     608             ; 96 (argv) + 512 (test area)
__bss_base:   equ     0x0B0000-__bss_size
__stack_top:  equ     __bss_base
__argv:       equ     __bss_base+0
t_bss_area:   equ     __bss_base+96
T_BSS_AREA_SIZE: equ  512

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
        ld      bc, T_BSS_AREA_SIZE
@loop:
        ld      a, b
        or      c
        jr      z, @alldone
        ld      a, (hl)
        or      a
        jr      nz, @fail
        inc     hl
        dec     bc
        jr      @loop
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

msg_pass:       db      "bss PASS", 13, 10, 0
msg_fail:       db      "bss FAIL", 13, 10, 0
