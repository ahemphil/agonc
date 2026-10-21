;;agonc-object 1
;;unit lib.c
; A library that defines _f and _g, where _g calls _f. over_main.s
; defines its own _f and calls _g: through lib's index, its _f is the
; one linked (lib's _f is never loaded); read whole, lib.s's _f is a
; duplicate, as it always was.
;;sect code _f g
_f:
        ld      hl, 1
        ret
;;sect code _g g
;;ref _f
_g:
        jp      _f
;;end
