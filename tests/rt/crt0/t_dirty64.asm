; crt0 test helper: t_dirty's job for t_bss64 and t_bss64p, a plain MOS
; program (no crt0) that fills [0x0B0000 - 0x10001, 0x0B0000) - the bss of
; both, 64K + 1 bytes ending where t_bss's does - with 0xA5, then returns 0
; WITHOUT writing port 0, so it can be chained before them in one emulator
; session (RAM carries over between the two programs).

        assume  adl=1
        org     0x040000

        jp      start
        align   64
        db      "MOS", 0, 1

start:
        ld      hl, 0x0B0000-0x10001
        ld      (hl), 0xA5
        ld      de, 0x0B0000-0x10000
        ld      bc, 0x10000             ; 64K + 1 bytes - 1 already written
        ldir
        ld      hl, 0
        ret
