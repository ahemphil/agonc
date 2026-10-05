;;agonc-object 1
;;unit twice.s
;;sect code _twice g
_twice:                         ; int twice(int n)
        ld      hl,3
        add     hl,sp
        ld      hl,(hl)         ; n: the first argument slot, above the return address
        add     hl,hl
        ret
;;end
