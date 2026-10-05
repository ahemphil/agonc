;;agonc-object 1
;;unit c.c 8df5
; Calls through implicit declarations (;;implicit) to functions that return
; long (l) and a structure (r) fail the link; one returning int (i) and
; assembly without ;;ret are fine.
;;sect code __start g
;;ret i
;;ref _getl
;;ref _gets
;;ref _geti
;;ref _asm
;;implicit _getl
;;implicit _gets
;;implicit _geti
;;implicit _asm
__start:
        call    _getl
        call    _gets
        call    _geti
        call    _asm
        ret
;;sect code _getl g
;;ret l
_getl:
        ret
;;sect code _gets g
;;ret r
_gets:
        ret
;;sect code _geti g
;;ret i
_geti:
        ret
;;sect code _asm g
_asm:
        ret
;;end
