;;agonc-object 1
;;unit coll.c
; _c2183 and _c3100 have the same 16-bit hash (7dbf), so they share a
; directory entry's hash in coll.idx; _c2183's record comes first. A link
; that needs only _c3100 must check the first candidate, pass over it,
; and load the second.
;;sect code _c2183 g
_c2183:
        ld      hl, 2183
        ret
;;sect code _c3100 g
_c3100:
        ld      hl, 3100
        ret
;;sect code _unused g
_unused:
        ret
;;end
