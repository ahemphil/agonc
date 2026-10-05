; Spike helper: terminate the emulator with exit code 0 (port 0).
; Prints a marker first so it is visible whether this line of autoexec ran.

        assume  adl=1
        org     0x040000

        jp      start
        align   64
        db      "MOS", 0, 1

start:
        ld      hl, msg
        push    hl
        ld      bc, 0
        xor     a
        rst.lis 18h
        pop     hl
p30:    ld      a, (hl)
        or      a
        jr      z, bye
        out0    (0x30), a
        inc     hl
        jr      p30
bye:
        xor     a
        out0    (0x00), a
        ld      hl, 0
        ret

msg:    db      "quit: reached", 13, 10, 0
