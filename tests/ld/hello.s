;;agonc-object 1
;;unit hello.c 6d74
; Hand-written stand-in for what cc2 will emit for:
;
;   int main(void) { puts("Hello from ld"); agon_emu_exit(42); return 0; }
;
; The string lives in _main's own pool after its code, addressed as
; _main+38 (the code below is exactly 38 bytes).
;;sect code _main g
;;ref _puts
;;ref _agon_emu_exit
_main:
        push    ix                      ; 2
        ld      ix, 0                   ; 5
        add     ix, sp                  ; 2
        ld      hl, _main+38            ; 4
        push    hl                      ; 1
        call    _puts                   ; 4
        pop     de                      ; 1
        ld      hl, 42                  ; 4
        push    hl                      ; 1
        call    _agon_emu_exit          ; 4
        pop     de                      ; 1
        ld      hl, 0                   ; 4
        ld      sp, ix                  ; 2
        pop     ix                      ; 2
        ret                             ; 1   total 38
        db      "Hello from ld", 0
;;end
