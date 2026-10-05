;;agonc-object 1
;;unit w.c 2331
; Weak references (object_format.md 8): this unit's references to _opt,
; _both and _nowhere are weak. _opt is defined in l.c but referenced
; strongly by nothing, so it is not linked and is 0; _both is referenced
; strongly from b.c, so it is linked; _nowhere is defined nowhere, which is
; not an error: it is 0.
;;wref _opt
;;wref _both
;;sect code __start g
;;ref _main
;;ref __stack_top
__start:
        ld      sp, __stack_top
        call    _main
        ret
;;sect code _main g
;;ref _opt
;;ref _both
;;ref _nowhere
;;ref _user
_main:
        ld      hl, _opt
        ld      hl, _both
        ld      hl, _nowhere
        call    _user
        ret
;;wref _nowhere
;;end
