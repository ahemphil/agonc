; crt0 test helper: a plain MOS program (no crt0) that fills the exact
; bss region t_bss.asm will use - [0x0AFDA0, 0x0B0000), 608 bytes, matching
; __bss_size=608 there - with 0xA5, then returns 0 WITHOUT writing port 0
; (so it can be chained before t_bss in the same emulator session via
; --cmd, with RAM state carrying over between the two program runs).

        assume  adl=1
        org     0x040000

        jp      start
        align   64
        db      "MOS", 0, 1

start:
        ld      hl, 0x0AFDA0
        ld      (hl), 0xA5
        ld      de, 0x0AFDA1
        ld      bc, 607                 ; 608 total bytes - 1 already written
        ldir
        ld      hl, 0
        ret
