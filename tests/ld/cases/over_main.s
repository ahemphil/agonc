;;agonc-object 1
;;unit main.c
;;sect code __start g
;;ref _g
__start:
        call    _g
        ret
;;sect code _f g
_f:
        ld      hl, 2
        ret
;;end
