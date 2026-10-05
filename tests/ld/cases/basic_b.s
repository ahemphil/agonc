;;agonc-object 1
;;unit b.c 1ef6
;;sect bss __s1ef6_state s 6
;;sect code _helper g
;;ref __s1ef6_state
;;ref __s1ef6_bump
_helper:
        call    __s1ef6_bump
        ret
;;sect code __s1ef6_bump s
;;ref __s1ef6_state
__s1ef6_bump:
        ld      hl, __s1ef6_state
        inc     (hl)
        ret
;;end
