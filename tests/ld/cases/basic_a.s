;;agonc-object 1
;;unit a.c f69f
; Exercises: callee-first ordering, a mutual-recursion cycle (one forward
; call), a code section reachable only through a data section, dropping an
; unreferenced section (whose own undefined reference must NOT be an
; error), bss equates, a ;;label referenced from another section, a
; cross-unit static reference, and comment/blank-line stripping.
;;sect code __start g
;;ref _main
;;ref __stack_top
;;label __exit
__start:
        ld      sp, __stack_top
        call    _main
__exit:
        ret
;;sect code _main g
;;ref _even
;;ref _table
;;ref _count
;;ref _helper
_main:
        ; a full-line comment, dropped by ld

        ld      hl, (_count)            ; an inline comment, kept
        call    _even
        call    _helper
        ret
;;sect code _even g
;;ref _odd
_even:
        call    _odd
        ret
;;sect code _odd g
;;ref _even
;;ref __exit
_odd:
        call    _even
        jp      __exit
;;sect code _unused g
;;ref _missing
_unused:
        call    _missing
        ret
;;sect data _table g
;;ref _leaf
_table:
        dl      _leaf
;;sect code _leaf g
_leaf:
        ret
;;sect bss _count g 3
;;sect bss _big g 100
;;end
