/* expr.c - expressions: parsing (standard C89 precedence), typing, the
 * usual arithmetic conversions, pointer scaling and constant folding: 24-bit
 * for int-sized types, 32-bit (through int32.c) for long, 64-bit (through
 * int64.c) for long long, IEEE (through softfp.c, which uses no floating
 * point) for float and double.
 *
 * Every node carries its C type. A node of array type stands for the array
 * object; its value (after decay) is its address - emit.c does the decay,
 * and value_type() here gives the decayed type for typing decisions.
 *
 * Called by stmt.c (expression(), assign_expr(), const_expr() and the
 * rest) with the lexer at the expression's first token; returns the root
 * node of a typed tree that stmt.c hands to emit.c. The parser is
 * recursive descent, one function per C89 grammar level, from the top:
 * expression (comma), assign_expr, conditional, binary (every binary
 * operator from || to *, by precedence climbing), cast_expr, unary,
 * postfix, primary.
 *
 * The tree is typed as it is built. Each operator's mk_* builder checks
 * its operands, applies the conversions C requires (each an explicit
 * CAST node where the representation changes, see conv), and folds the
 * operation at once when both operands are constants, so "2 * 3 + x"
 * becomes a node for 6 + x before the parser even sees the x. Folding is
 * exact to the target: int arithmetic wraps at 24 bits on every host,
 * and floating constants are computed by softfp.c, the same code the
 * runtime library's floating-point helpers are built from, so a folded
 * constant has exactly the bits the operation would give at run time.
 *
 * Bit-fields: a read stays one EN_BITF node, which emit.c expands into a
 * load, a shift and a mask or sign extension; a store (and ++ or --) is
 * lowered here into a read-modify-write of the field's bytes, built from
 * ordinary nodes.
 *
 * Sections: nodes and constants, bit-fields, constant folding, node
 * builders (conversions and operators), calls, the stdarg and GCC
 * built-ins, primary and postfix, unary and cast, binary operators and
 * the conditional, assignment, constant expressions.
 */

#include <stdio.h>
#include <string.h>
#include "int24.h"
#include "cc1.h"

struct enode nodes[MAX_NODES];
struct sf64 fbits[MAX_NODES];   /* a floating constant node's bits (a float's in lo) */
int nnodes;
int peak_nodes;
int in_function;
int fp_used;
int ll_used;

/* ---- nodes and constants ------------------------------------------------------ */

/* A new node. It also notes, for the function being compiled, any float,
 * double or long long value: func_end then writes the R records that pull
 * the printf and scanf conversions for those types into the program. */
int node(int op, int type, int a, int b, int val)
{
    if (nnodes >= MAX_NODES) {
        fprintf(stderr, "%s:%d: error: expression too complex (a cc1 table limit; raise MAX_NODES)\n",
                cur_file, tok_line);
        fatal("");
    }
    nodes[nnodes].op = op;
    nodes[nnodes].type = type;
    nodes[nnodes].a = a;
    nodes[nnodes].b = b;
    nodes[nnodes].c = -1;
    nodes[nnodes].val = val;
    nodes[nnodes].hi8 = 0;
    nnodes++;
    if (in_function && is_floating(type))
        fp_used = 1;
    if (in_function && is_llong(type))
        ll_used = 1;
    if (nnodes > peak_nodes)
        peak_nodes = nnodes;
    return nnodes - 1;
}

/* A constant v (an int-sized value) of type t; for a long t, v is extended
 * by t's signedness (hi8 all ones for a negative signed value). */
int num(int v, int t)
{
    int n;

    n = node(EN_NUM, t, -1, -1, wrap24(v));
    nodes[n].hi8 = !is_unsigned(t) && nodes[n].val < 0 ? 255 : 0;
    return n;
}

static int fold_cast(int v, int t);

/* A floating constant of type t. */
static int fnum(const struct sf64 *v, int t)
{
    int n;

    n = node(EN_NUM, t, -1, -1, 0);
    fbits[n] = *v;
    return n;
}

/* A floating constant's value as a binary64 (a float's widened: exact,
 * since every binary32 value is a binary64 value). */
static void fval(int n, struct sf64 *r)
{
    if (types[nodes[n].type].kind == TY_FLOAT)
        sf64_from_f32(r, fbits[n].lo);
    else
        *r = fbits[n];
}

/* 0.0 of floating type t */
static int fzero(int t)
{
    struct sf64 z;

    z.hi = 0;
    z.lo = 0;
    return fnum(&z, t);
}

/* A long long constant of type t: its 64 bits in fbits (as a double's),
 * and its low 24 and next 8 in val and hi8, as a long's, for the code that
 * only wants those. */
static int qnum(const struct i64 *v, int t)
{
    int n;
    struct i32 w;

    i32_set(&w, (int)(v->lo >> 16 & 0xFFFF), (int)(v->lo & 0xFFFF));
    n = node(EN_NUM, t, -1, -1, i32_low24(&w));
    nodes[n].hi8 = i32_high8(&w);
    fbits[n].lo = v->lo;
    fbits[n].hi = v->hi;
    return n;
}

static void cval(int n, struct i32 *r);

/* An integer constant node's value in 64 bits, extended by its type: a
 * long long's straight from fbits, any other through its 32 bits, the
 * upper half all ones if it is signed and bit 31 is set. */
static void qval(int n, struct i64 *r)
{
    struct i32 w;

    if (is_llong(nodes[n].type)) {
        i64_set(r, fbits[n].hi, fbits[n].lo);
        return;
    }
    cval(n, &w);
    i64_set(r, 0, (unsigned long)w.hi << 16 | (unsigned long)w.lo);
    if (w.hi >= 0x8000 && !is_unsigned(nodes[n].type))
        r->hi = 0xFFFFFFFFUL;           /* (not i64_from_long: AgDev's LTO fails on it here) */
}

/* 0 of type long long t */
static int qzero(int t)
{
    struct i64 z;

    i64_set(&z, 0, 0);
    return qnum(&z, t);
}

/* A constant of type t from a 32-bit value, reduced to t's width: a long
 * keeps all 32 bits, an int-sized type its low 24 cut to its own width. */
static int num32(struct i32 *v, int t)
{
    int n;

    if (!is_long(t))
        return num(fold_cast(i32_low24(v), t), t);
    n = node(EN_NUM, t, -1, -1, i32_low24(v));
    nodes[n].hi8 = i32_high8(v);
    return n;
}

/* A constant node's 32 bits (an int-sized one extended by its type; a
 * long long's low 32). */
static void cval(int n, struct i32 *r)
{
    if (!is_long(nodes[n].type) && !is_llong(nodes[n].type)) {
        if (is_unsigned(nodes[n].type))
            i32_from_uint(r, nodes[n].val);
        else
            i32_from_int(r, nodes[n].val);
        return;
    }
    i32_join(r, nodes[n].hi8, nodes[n].val);
}

/* Whether constant n is nonzero, as a condition tests it. */
static int const_nonzero(int n)
{
    struct sf64 v;

    if (is_floating(nodes[n].type)) {
        fval(n, &v);
        return (v.hi & 0x7FFFFFFFUL) != 0 || v.lo != 0;         /* +0 and -0 are zero; a NaN is not */
    }
    if (is_llong(nodes[n].type))
        return fbits[n].hi != 0 || fbits[n].lo != 0;
    return nodes[n].val != 0 || (is_long(nodes[n].type) && nodes[n].hi8 != 0);
}

/* A value's type: arrays and functions decayed, qualifiers dropped. */
static int value_type(int n)
{
    return unqual(decay(nodes[n].type));
}

/* value_type for stmt.c and emit.c */
int value_type_of(int n)
{
    return value_type(n);
}

/* folded to a constant (every constant is an EN_NUM node) */
static int is_const(int n)
{
    return nodes[n].op == EN_NUM;
}

/* A null pointer constant (C89 3.2.2.3): an integer constant equal to 0,
 * or one cast to void * (the library's NULL is ((void *)0)); conv folds
 * that cast to a constant of type void *. */
static int is_null(int n)
{
    int t;

    t = nodes[n].type;
    return is_const(n) && !const_nonzero(n) && (is_integer(t) || (is_pointer(t) && types[t].base == T_VOID));
}

/* An lvalue as assignment and ++ need it: a node that designates an
 * object and is not an array or function (those decay instead). const is
 * checked by the callers. */
int is_lvalue(int n)
{
    int op;
    int k;

    op = nodes[n].op;
    k = types[nodes[n].type].kind;
    if (k == TY_ARRAY || k == TY_FUNC)
        return 0;
    return op == EN_GVAR || op == EN_LVAR || op == EN_DEREF || op == EN_BITF;
}

/* Whether evaluating n twice is the same as once (nothing is called,
 * assigned or incremented), checked over the whole subtree. */
static int pure(int n)
{
    int op;

    if (n < 0)
        return 1;
    op = nodes[n].op;
    if (op == EN_CALL || op == EN_ASSIGN || op == EN_ASGOP || op == EN_INC)
        return 0;
    if (op == EN_NUM || op == EN_STR || op == EN_GVAR || op == EN_LVAR)
        return 1;
    return pure(nodes[n].a) && pure(nodes[n].b) && (op != EN_COND || pure(nodes[n].c));
}

static int conv(int n, int t);

/* ---- bit-fields ----------------------------------------------------------------- */

/* An EN_BITF node (member() makes them) holds in a the address of the
 * field's bytes: one byte for a field of up to 8 bits, else three (an int
 * at the field's first byte). val is its first bit, c its width, hi8 1 if
 * it is signed. */

/* A bit-field's value from its unit (x, an unsigned int holding the field
 * at bit 0 upwards): masked, or sign-extended if the field is signed. The
 * sign extension is the shift-left-then-arithmetic-shift-right trick:
 * move the field's top bit to bit 23, then shift back with SHRS, which
 * copies that bit into everything above the field. */
static int bf_extend(int x, int width, int sign)
{
    if (sign)
        return node(EN_BIN, T_INT, node(EN_BIN, T_INT, x, num(24 - width, T_INT), B_SHL),
                    num(24 - width, T_INT), B_SHRS);
    return node(EN_BIN, width < 24 ? T_INT : T_UINT, x, num(width < 24 ? (1 << width) - 1 : -1, T_UINT), B_AND);
}

/* A bit-field lvalue whose address can be used twice: a temporary holds a
 * complex one; *pre gets the assignment to it, or -1. A store reads and
 * writes the field's bytes, so an address with side effects (p++->f = 1)
 * must be evaluated once and kept. */
static int bf_stable(int lv, int *pre)
{
    int tmp;
    int p;

    *pre = -1;
    p = nodes[lv].a;
    if (pure(p))
        return lv;
    tmp = new_temp(nodes[p].type);
    *pre = node(EN_ASSIGN, nodes[p].type, node(EN_LVAR, nodes[p].type, -1, -1, tmp), p, 0);
    p = node(EN_BITF, nodes[lv].type, node(EN_LVAR, nodes[p].type, -1, -1, tmp), -1, nodes[lv].val);
    nodes[p].c = nodes[lv].c;           /* the width and signedness go with it */
    nodes[p].hi8 = nodes[lv].hi8;
    return p;
}

/* Store v into bit-field lv (whose address is pure): its unit's other bits
 * kept; the value is the field as stored, read back. The tree is
 *     (*unit = (*unit & ~(mask << off)) | ((v & mask) << off)), lv
 * with the unit a byte or a 3-byte int, so the bits beside the field are
 * written back as they were read. */
static int bf_store(int lv, int v)
{
    int p;
    int ut;
    int off;
    int w;
    int mask;
    int keep;
    int bits;
    int unit;

    p = nodes[lv].a;
    off = nodes[lv].val;
    w = nodes[lv].c;
    ut = w <= 8 ? T_UCHAR : T_UINT;
    mask = w < 24 ? (1 << w) - 1 : 0xFFFFFF;
    unit = node(EN_DEREF, ut, p, -1, 0);
    keep = node(EN_BIN, T_UINT, conv(unit, T_UINT), num(wrap24(~(mask << off)), T_UINT), B_AND);
    bits = node(EN_BIN, T_UINT, node(EN_BIN, T_UINT, conv(v, T_UINT), num(wrap24(mask), T_UINT), B_AND),
                num(off, T_INT), B_SHL);
    v = node(EN_ASSIGN, ut, node(EN_DEREF, ut, p, -1, 0),
             conv(node(EN_BIN, T_UINT, keep, bits, B_OR), ut), 0);
    return node(EN_COMMA, nodes[lv].type, v, lv, 0);
}

/* (pre, n) if bf_stable made a temporary, else n */
static int with_pre(int pre, int n)
{
    return pre < 0 ? n : node(EN_COMMA, nodes[n].type, pre, n, 0);
}

/* ---- constant folding (exact 24-bit semantics on any host) ------------------ */

/* v's 24 bits as an unsigned value. Folding computes in unsigned, where
 * overflow wraps (signed overflow is undefined in C), then wrap24 makes
 * the low 24 bits a canonical int again; on a 32-bit host the high bits
 * are simply discarded. */
static unsigned u24(int v)
{
    unsigned u;

    u = v;
    return u & 0xFFFFFFu;
}

/* a op b on canonical int-sized values; a comparison gives 0 or 1 */
static int fold(int op, int a, int b)
{
    switch (op) {
    case B_ADD: return wrap24(u24(a) + u24(b));
    case B_SUB: return wrap24(u24(a) - u24(b));
    case B_MUL: return wrap24(u24(a) * u24(b));
    case B_DIVS:
    case B_REMS:
        if (b == 0) {
            error("division by zero in a constant expression");
            return 0;
        }
        /* INT_MIN / -1 overflows, and in the compiler's own 24-bit int
         * on the Agon too, so it is not left to the C division: the
         * two's complement wrapped result, INT_MIN remainder 0 */
        if (a == -8388607 - 1 && b == -1)
            return op == B_DIVS ? a : 0;
        return op == B_DIVS ? a / b : a % b;
    case B_DIVU:
    case B_REMU:
        if (b == 0) {
            error("division by zero in a constant expression");
            return 0;
        }
        return wrap24(op == B_DIVU ? u24(a) / u24(b) : u24(a) % u24(b));
    /* shifts: SHRS relies on the host's >> of a negative int being
     * arithmetic (C leaves it implementation-defined). A count outside
     * 0..23 is undefined in C; mk_arith warns of a constant one. */
    case B_SHL: return wrap24(u24(a) << (b & 31));
    case B_SHRS: return wrap24(a >> (b & 31));
    case B_SHRU: return wrap24(u24(a) >> (b & 31));
    case B_AND: return wrap24(u24(a) & u24(b));
    case B_OR: return wrap24(u24(a) | u24(b));
    case B_XOR: return wrap24(u24(a) ^ u24(b));
    case B_EQ: return a == b;
    case B_NE: return a != b;
    case B_LTS: return a < b;
    case B_LTU: return u24(a) < u24(b);
    case B_LES: return a <= b;
    case B_LEU: return u24(a) <= u24(b);
    case B_GTS: return a > b;
    case B_GTU: return u24(a) > u24(b);
    case B_GES: return a >= b;
    case B_GEU: return u24(a) >= u24(b);
    }
    return 0;
}

/* Convert an int-sized constant to integer type t (narrowing keeps the
 * low bits). The signed cases sign-extend with the xor-subtract trick:
 * ((v & 255) ^ 128) - 128 flips the sign bit and subtracts its weight,
 * which maps 0..127 to itself and 128..255 to -128..-1 on any int width. */
static int fold_cast(int v, int t)
{
    int k;

    k = types[t].kind;
    if (k == TY_CHAR || k == TY_SCHAR)
        return ((v & 255) ^ 128) - 128;
    if (k == TY_UCHAR)
        return v & 255;
    if (k == TY_SHORT)
        return ((v & 65535) ^ 32768) - 32768;
    if (k == TY_USHORT)
        return v & 65535;
    return wrap24(v);
}

/* The same operators on 32-bit constants (int32.c, which works in 16-bit
 * halves so that any host computes the same); a comparison gives 0 or 1. */
static void fold32(int op, struct i32 *a, struct i32 *b, struct i32 *r)
{
    struct i32 q;
    int c;

    switch (op) {
    case B_ADD: i32_add(r, a, b); return;
    case B_SUB: i32_sub(r, a, b); return;
    case B_MUL: i32_mul(r, a, b); return;
    case B_AND: i32_and(r, a, b); return;
    case B_OR: i32_or(r, a, b); return;
    case B_XOR: i32_xor(r, a, b); return;
    case B_SHL: i32_shl(r, a, b->lo & 31); return;
    case B_SHRS: i32_shr(r, a, b->lo & 31, 1); return;
    case B_SHRU: i32_shr(r, a, b->lo & 31, 0); return;
    case B_DIVS: case B_REMS: case B_DIVU: case B_REMU:
        if (i32_is_zero(b)) {
            error("division by zero in a constant expression");
            i32_set(r, 0, 0);
            return;
        }
        if (op == B_DIVS || op == B_REMS)
            i32_divs(&q, r, a, b);
        else
            i32_divu(&q, r, a, b);
        if (op == B_DIVS || op == B_DIVU)
            *r = q;
        return;
    }
    c = i32_cmp(a, b, op == B_LTS || op == B_LES || op == B_GTS || op == B_GES);
    switch (op) {
    case B_EQ: c = c == 0; break;
    case B_NE: c = c != 0; break;
    case B_LTS: case B_LTU: c = c < 0; break;
    case B_LES: case B_LEU: c = c <= 0; break;
    case B_GTS: case B_GTU: c = c > 0; break;
    default: c = c >= 0; break;
    }
    i32_set(r, 0, c);
}

/* The same on 64-bit constants (int64.c); a comparison gives 0 or 1. */
static int fold64(int op, int t, int a, int b)
{
    struct i64 x;
    struct i64 y;
    struct i64 r;
    struct i64 q;
    int c;

    qval(a, &x);
    qval(b, &y);
    switch (op) {
    case B_ADD: i64_add(&r, &x, &y); return qnum(&r, t);
    case B_SUB: i64_sub(&r, &x, &y); return qnum(&r, t);
    case B_MUL: i64_mul(&r, &x, &y); return qnum(&r, t);
    case B_AND: i64_and(&r, &x, &y); return qnum(&r, t);
    case B_OR: i64_or(&r, &x, &y); return qnum(&r, t);
    case B_XOR: i64_xor(&r, &x, &y); return qnum(&r, t);
    case B_SHL: i64_shl(&r, &x, (int)(y.lo & 63)); return qnum(&r, t);
    case B_SHRS: i64_shr(&r, &x, (int)(y.lo & 63), 1); return qnum(&r, t);
    case B_SHRU: i64_shr(&r, &x, (int)(y.lo & 63), 0); return qnum(&r, t);
    case B_DIVS: case B_REMS: case B_DIVU: case B_REMU:
        if (i64_is_zero(&y)) {
            error("division by zero in a constant expression");
            return qzero(t);
        }
        if (op == B_DIVS || op == B_REMS)
            i64_divs(&q, &r, &x, &y);
        else
            i64_divu(&q, &r, &x, &y);
        return qnum(op == B_DIVS || op == B_DIVU ? &q : &r, t);
    }
    c = i64_cmp(&x, &y, op == B_LTS || op == B_LES || op == B_GTS || op == B_GES);
    switch (op) {
    case B_EQ: c = c == 0; break;
    case B_NE: c = c != 0; break;
    case B_LTS: case B_LTU: c = c < 0; break;
    case B_LES: case B_LEU: c = c <= 0; break;
    case B_GTS: case B_GTU: c = c > 0; break;
    default: c = c >= 0; break;
    }
    return num(c, T_INT);
}

/* The operators on floating constants of one type, as IEEE computes them
 * (softfp.c); a comparison gives 0 or 1 (unordered: only != is true). */
static int fold_float(int op, int t, int a, int b)
{
    struct sf64 x;
    struct sf64 y;
    struct sf64 r;
    int c;

    fval(a, &x);
    fval(b, &y);
    if (op == B_ADD || op == B_SUB || op == B_MUL || op == B_DIVS) {
        /* only a warning: IEEE defines x / 0 (an infinity or a NaN) */
        if (op == B_DIVS && (y.hi & 0x7FFFFFFFUL) == 0 && y.lo == 0)
            warning("division by zero in a constant expression");
        r.hi = 0;
        if (types[value_type(a)].kind == TY_FLOAT) {
            r.lo = op == B_ADD ? sf32_add(fbits[a].lo, fbits[b].lo) : op == B_SUB ? sf32_sub(fbits[a].lo, fbits[b].lo)
                   : op == B_MUL ? sf32_mul(fbits[a].lo, fbits[b].lo) : sf32_div(fbits[a].lo, fbits[b].lo);
        } else if (op == B_ADD) {
            sf64_add(&r, &x, &y);
        } else if (op == B_SUB) {
            sf64_sub(&r, &x, &y);
        } else if (op == B_MUL) {
            sf64_mul(&r, &x, &y);
        } else {
            sf64_div(&r, &x, &y);
        }
        return fnum(&r, t);
    }
    /* sf64_cmp gives -1, 0, 1, or 2 for unordered, which every test but
     * != then fails; the floating types have only the signed forms */
    c = sf64_cmp(&x, &y);
    switch (op) {
    case B_EQ: c = c == 0; break;
    case B_NE: c = c != 0; break;
    case B_LTS: c = c == -1; break;
    case B_LES: c = c == -1 || c == 0; break;
    case B_GTS: c = c == 1; break;
    default: c = c == 1 || c == 0; break;
    }
    return num(c, T_INT);
}

/* ---- node builders ------------------------------------------------------------- */

/* a op b; both operands already have the operation's type (a shift's
 * count is an int), and t is the result's. Two constants fold at once, in
 * the arithmetic of the operands' type; otherwise an EN_BIN node. */
static int bin(int op, int t, int a, int b)
{
    struct i32 x;
    struct i32 y;
    struct i32 r;

    if (is_const(a) && is_const(b)) {
        if (is_floating(value_type(a)))
            return fold_float(op, t, a, b);
        if (is_llong(value_type(a)))
            return fold64(op, t, a, b);
        if (!is_long(value_type(a)))
            return num(fold(op, nodes[a].val, nodes[b].val), t);
        cval(a, &x);
        cval(b, &y);
        fold32(op, &x, &y, &r);
        return num32(&r, t);
    }
    return node(EN_BIN, t, a, b, op);
}

/* The usual arithmetic conversions (C89 3.2.1.5) with this target's widths:
 * unsigned long, else long (which holds every unsigned int), else unsigned
 * int, else int; above them C99's unsigned long long, else long long
 * (which holds every unsigned long). Each operand is promoted first, and
 * any floating operand makes the result floating, the wider one winning. */
static int arith_type(int a, int b)
{
    int ka;
    int kb;

    ka = types[promote(value_type(a))].kind;
    kb = types[promote(value_type(b))].kind;
    if (ka == TY_LDOUBLE || kb == TY_LDOUBLE)
        return T_LDOUBLE;
    if (ka == TY_DOUBLE || kb == TY_DOUBLE)
        return T_DOUBLE;
    if (ka == TY_FLOAT || kb == TY_FLOAT)
        return T_FLOAT;                 /* float stays float (C89 3.2.1.5) */
    if (ka == TY_ULLONG || kb == TY_ULLONG)
        return T_ULLONG;
    if (ka == TY_LLONG || kb == TY_LLONG)
        return T_LLONG;
    if (ka == TY_ULONG || kb == TY_ULONG)
        return T_ULONG;
    if (ka == TY_LONG || kb == TY_LONG)
        return T_LONG;
    if (ka == TY_UINT || kb == TY_UINT)
        return T_UINT;
    return T_INT;
}

/* A constant n of arithmetic type s converted to t, one of them floating
 * (C89 3.2.1.3-3.2.1.4): an integer exactly to double, rounded to float; a
 * floating value truncated toward zero to an integer (beyond the range
 * C89 leaves undefined: saturated, then narrowed). */
static int fold_conv(int n, int s, int t)
{
    struct sf64 d;
    struct i32 v;
    struct i64 q;
    unsigned long u;

    if (is_floating(s)) {
        fval(n, &d);
    } else if (is_llong(s)) {
        qval(n, &q);
        if (types[t].kind == TY_FLOAT) {
            d.lo = sf32_from_long64(q.hi, q.lo, !is_unsigned(s));      /* rounded once, from the integer */
            d.hi = 0;
            return fnum(&d, t);
        }
        sf64_from_long64(&d, q.hi, q.lo, !is_unsigned(s));
    } else {
        cval(n, &v);
        u = (unsigned long)v.hi << 16 | (unsigned long)v.lo;
        sf64_from_long(&d, u, !is_unsigned(s));
    }
    if (is_llong(t)) {
        sf64_to_long64(&d, !is_unsigned(t), &q.hi, &q.lo);
        return qnum(&q, t);
    }
    if (types[t].kind == TY_FLOAT) {
        d.lo = sf32_from_f64(&d);       /* exact in double first: rounded once */
        d.hi = 0;
        return fnum(&d, t);
    }
    if (is_floating(t))
        return fnum(&d, t);
    u = sf64_to_long(&d, !is_unsigned(t));
    i32_set(&v, (int)(u >> 16 & 0xFFFF), (int)(u & 0xFFFF));
    return num32(&v, t);
}

/* n converted to arithmetic type t, as C89 3.2.1 says. Between int-sized
 * types of one width nothing changes (the IR has no types); between widths
 * and kinds a CAST says how (emit.c writes EXT or CV). An int-sized value
 * is always canonical (abi.md 3: char and short already extended to 24
 * bits), so widening to int or unsigned int needs no code at all; only
 * narrowing to 1 or 2 bytes (EXT re-extends the low bytes) and a change
 * between the I, L, Q, F and D kinds (CV) do. A constant is converted
 * here instead, into a new constant node. */
static int conv(int n, int t)
{
    int s;
    struct i32 v;
    struct i64 q;

    s = value_type(n);
    if (s == t)
        return n;
    if (is_floating(s) || is_floating(t)) {
        if (is_const(n) && is_arith(s))
            return fold_conv(n, s, t);
        return node(EN_CAST, t, n, -1, 0);
    }
    if (is_const(n) && is_integer(s) && is_llong(t)) {
        qval(n, &q);
        return qnum(&q, t);
    }
    if (is_const(n) && is_integer(s)) {
        cval(n, &v);                    /* a long long's low 32 bits, as val and hi8 hold them */
        return num32(&v, t);
    }
    if (is_llong(s) || is_llong(t) || is_long(s) != is_long(t) || type_size(t) < 3)
        return node(EN_CAST, t, n, -1, 0);
    return n;
}

/* An integer index or count as an int (pointers and shift counts are
 * int-sized): a long or long long is cut to its low 24 bits. */
static int int_operand(int n)
{
    return is_long(value_type(n)) || is_llong(value_type(n)) ? conv(n, T_INT) : n;
}

static void need_integer(int n, char *what)
{
    if (!is_integer(value_type(n)))
        error_s("operand must be an integer: ", what);
}

static void need_arith(int n, char *what)
{
    if (!is_arith(value_type(n)))
        error_s("operand must be a number: ", what);
}

/* A floating or long long value as a condition: n != 0, an int, so that
 * jumps and the logical operators only ever test integers. Testing the
 * value itself would be wrong for both: a D or Q value in the IR is an
 * address, and -0.0 is a zero whose bits are not all zero. Other scalars
 * are returned as they are. */
int truth(int n)
{
    int t;

    t = value_type(n);
    if (is_llong(t))
        return bin(B_NE, T_INT, n, qzero(t));
    if (!is_floating(t))
        return n;
    return bin(B_NE, T_INT, n, fzero(t));
}

static void need_scalar(int n, char *what)
{
    if (!is_scalar(value_type(n)))
        error_s("operand must be a number or pointer: ", what);
}

/* n (an integer) times the size of pointer type p's target: pointer
 * scaling, which makes p + 1 address the next element, not the next
 * byte. A constant n folds to the byte offset. */
static int scale(int n, int p)
{
    int size;

    size = type_size(types[p].base);
    if (types[types[p].base].kind == TY_VOID || size == 0)
        error("arithmetic on a pointer to void or to an incomplete type");
    n = int_operand(n);
    if (size <= 1)
        return n;
    return bin(B_MUL, T_INT, n, num(size, T_INT));
}

/* a + b: pointer plus integer (either way round, the pointer first in the
 * node), or two numbers in their common type */
static int mk_add(int a, int b)
{
    int ta;
    int tb;

    ta = value_type(a);
    tb = value_type(b);
    if (is_pointer(ta) && is_integer(tb))
        return bin(B_ADD, ta, a, scale(b, ta));
    if (is_integer(ta) && is_pointer(tb))
        return bin(B_ADD, tb, b, scale(a, tb));
    if (!is_arith(ta) || !is_arith(tb)) {
        error("invalid operands to '+'");
        return a;
    }
    ta = arith_type(a, b);
    return bin(B_ADD, ta, conv(a, ta), conv(b, ta));
}

/* a - b: pointer minus integer, pointer minus pointer (the byte
 * difference divided by the element size, an int, as ptrdiff_t is), or
 * two numbers */
static int mk_sub(int a, int b)
{
    int ta;
    int tb;
    int d;
    int size;

    ta = value_type(a);
    tb = value_type(b);
    if (is_pointer(ta) && is_integer(tb))
        return bin(B_SUB, ta, a, scale(b, ta));
    if (is_pointer(ta) && is_pointer(tb)) {
        if (unqual(types[ta].base) != unqual(types[tb].base))
            error("subtracting pointers to different types");
        d = bin(B_SUB, T_INT, a, b);
        size = type_size(types[ta].base);
        if (size > 1)
            d = bin(B_DIVS, T_INT, d, num(size, T_INT));
        return d;
    }
    if (!is_arith(ta) || !is_arith(tb)) {
        error("invalid operands to '-'");
        return a;
    }
    ta = arith_type(a, b);
    return bin(B_SUB, ta, conv(a, ta), conv(b, ta));
}

/* * and / on numbers; % << >> & | ^ on integers */
static int mk_arith(int tk, int a, int b)
{
    int t;
    int u;

    if (tk == TK_P + '*' || tk == TK_P + '/') {
        need_arith(a, "arithmetic");
        need_arith(b, "arithmetic");
    } else {
        need_integer(a, "arithmetic");
        need_integer(b, "arithmetic");
    }
    if (tk == P_SHL || tk == P_SHR) {
        /* a shift is not balanced (C89 3.3.7): the result has the left
         * operand's promoted type, and the count is promoted on its own
         * and taken as an int; >> is logical for an unsigned type and
         * arithmetic for a signed one (abi.md 2) */
        t = promote(value_type(a));
        a = conv(a, t);
        b = int_operand(conv(b, promote(value_type(b))));
        if (is_const(b) && (nodes[b].val < 0 || nodes[b].val > (is_llong(t) ? 63 : is_long(t) ? 31 : 23)))
            warning(is_llong(t) ? "shift count outside 0..63" : is_long(t) ? "shift count outside 0..31"
                    : "shift count outside 0..23");
        if (tk == P_SHL)
            return bin(B_SHL, t, a, b);
        return bin(is_unsigned(t) ? B_SHRU : B_SHRS, t, a, b);
    }
    /* both operands to their common type, which also picks the signed
     * or unsigned form of / and % */
    t = arith_type(a, b);
    a = conv(a, t);
    b = conv(b, t);
    u = is_unsigned(t);
    switch (tk) {
    case TK_P + '*': return bin(B_MUL, t, a, b);
    case TK_P + '/': return bin(u ? B_DIVU : B_DIVS, t, a, b);
    case TK_P + '%': return bin(u ? B_REMU : B_REMS, t, a, b);
    case TK_P + '&': return bin(B_AND, t, a, b);
    case TK_P + '|': return bin(B_OR, t, a, b);
    case TK_P + '^': return bin(B_XOR, t, a, b);
    }
    return a;
}

/* Warnings for comparing pointers to different types, or a pointer with
 * an integer other than a null pointer constant (C89 3.3.8, 3.3.9). */
static void check_ptr_compare(int a, int b)
{
    int ta;
    int tb;

    ta = value_type(a);
    tb = value_type(b);
    if (is_pointer(ta) && is_pointer(tb)) {
        if (unqual(types[ta].base) != unqual(types[tb].base) && !is_void(types[ta].base)
            && !is_void(types[tb].base))
            warning("comparison of distinct pointer types");
    } else if (is_pointer(ta) || is_pointer(tb)) {
        if (!is_null(is_pointer(ta) ? b : a))
            warning("comparison between a pointer and an integer");
    }
}

/* a relational or equality operator: an int 0 or 1. Numbers compare in
 * their common type, pointers as unsigned 24-bit addresses. */
static int mk_compare(int tk, int a, int b)
{
    int u;

    need_scalar(a, "comparison");
    need_scalar(b, "comparison");
    if ((is_floating(value_type(a)) || is_floating(value_type(b)))
        && !(is_arith(value_type(a)) && is_arith(value_type(b)))) {
        error("comparison between a pointer and a floating value");
        return num(0, T_INT);
    }
    check_ptr_compare(a, b);
    if (is_arith(value_type(a)) && is_arith(value_type(b))) {
        u = arith_type(a, b);
        a = conv(a, u);
        b = conv(b, u);
        u = is_unsigned(u);
    } else {
        /* a pointer against a pointer, or against a null constant */
        a = int_operand(a);
        b = int_operand(b);
        u = 1;
    }
    switch (tk) {
    case P_EQ: return bin(B_EQ, T_INT, a, b);
    case P_NE: return bin(B_NE, T_INT, a, b);
    case TK_P + '<': return bin(u ? B_LTU : B_LTS, T_INT, a, b);
    case TK_P + '>': return bin(u ? B_GTU : B_GTS, T_INT, a, b);
    case P_LE: return bin(u ? B_LEU : B_LES, T_INT, a, b);
    case P_GE: return bin(u ? B_GEU : B_GES, T_INT, a, b);
    }
    return a;
}

/* Convert n to type t for an assignment, argument or return ("what"), as
 * if by assignment (C89 3.3.16.1). A mismatch of pointer types, or between
 * a pointer and an integer, is a warning; a value of the wrong kind
 * altogether (a struct for a number, say) is an error. Struct values are
 * not converted, only checked. A pointer to or from an int-sized integer
 * keeps its bits, so it needs no node; a long gets a CAST (abi.md 2). */
int convert(int n, int t, char *what)
{
    int s;
    char buf[80];

    s = value_type(n);
    if (types[t].kind == TY_VOID)
        return n;
    if (is_struct(t) || is_struct(s)) {
        if (s != unqual(t))
            error_s("incompatible struct value in ", what);
        else if (type_size(s) == 0)
            error_s("incomplete struct value in ", what);
        return n;
    }
    t = unqual(t);
    if (is_floating(t)) {
        if (!is_arith(s)) {
            error_s("incompatible value in ", what);
            return n;
        }
        return conv(n, t);
    }
    if (is_integer(t)) {
        if (is_floating(s))
            return conv(n, t);
        if (is_pointer(s)) {
            sprintf(buf, "pointer converted to integer without a cast (%s)", what);
            warning(buf);
            return is_long(t) || is_llong(t) ? node(EN_CAST, t, n, -1, 0) : n;
        } else if (!is_integer(s)) {
            error_s("incompatible value in ", what);
            return n;
        }
        return conv(n, t);
    }
    if (is_pointer(t)) {
        if (is_pointer(s)) {
            if (unqual(types[s].base) != unqual(types[t].base) && !is_void(types[s].base)
                && !is_void(types[t].base)
                && !(types[types[s].base].kind == TY_FUNC && types[types[t].base].kind == TY_FUNC
                     && compatible_funcs(types[s].base, types[t].base))) {
                sprintf(buf, "incompatible pointer types (%s)", what);
                warning(buf);
            } else if (types[types[s].base].qual & ~types[types[t].base].qual) {
                sprintf(buf, "%s discards a const or volatile qualifier", what);
                warning(buf);
            }
        } else if (is_integer(s)) {
            if (!is_null(n)) {
                sprintf(buf, "integer converted to pointer without a cast (%s)", what);
                warning(buf);
            }
            return int_operand(n);
        } else {
            error_s("incompatible value in ", what);
        }
        return n;
    }
    error_s("cannot convert to this type in ", what);
    return n;
}

/* ---- primary and postfix -------------------------------------------------------- */

/* A call, at the arguments' "(" already read: to the function fsym, or
 * (fsym -1) through fp, a pointer to a function of type ft. Arguments
 * matching prototype parameters are converted as by assignment; the rest
 * (those for "...", and all of them without a prototype, nparams -1) get
 * the default argument promotions (abi.md 4). A struct, double or long
 * long result needs an object for the callee to fill: a temporary local,
 * whose address emit.c passes as the hidden first argument. */
static int call(int fsym, int ft, int fp)
{
    int args[MAX_ARGS_CALL + 1];
    int n;
    int i;
    int list;
    int t;
    char buf[100];
    char *fname;

    fname = fsym >= 0 ? name_str(globals[fsym].name) : "a function through a pointer";
    n = 0;
    if (tok != TK_P + ')') {
        for (;;) {
            if (n > MAX_ARGS_CALL)
                fatal("more than 31 arguments in a call");
            args[n] = assign_expr();
            n++;
            if (!accept(TK_P + ','))
                break;
        }
    }
    expect(TK_P + ')', "')' after arguments");
    if (types[ft].nparams >= 0 && (n < types[ft].nparams || (n > types[ft].nparams && !types[ft].variadic))) {
        sprintf(buf, "wrong number of arguments to %s (%d given, %d expected)", fname, n, types[ft].nparams);
        error(buf);
    }
    for (i = 0; i < n; i++) {
        if (i < types[ft].nparams) {
            args[i] = convert(args[i], plist[types[ft].params + i], "argument");
        } else {
            t = value_type(args[i]);
            if (is_struct(t) && type_size(t) == 0)
                error("argument to '...' is an incomplete struct");
            else if (!is_scalar(t) && !is_struct(t))
                error("argument to '...' must be a number, pointer or struct");
            else if (is_integer(t))
                args[i] = conv(args[i], promote(t));     /* char and short become int */
            else if (types[t].kind == TY_FLOAT)
                args[i] = conv(args[i], T_DOUBLE);       /* float becomes double */
        }
    }
    /* chain last argument first: that is the order they are evaluated and
     * pushed (right to left, abi.md 4), so the first lands at ix+6 */
    list = -1;
    for (i = 0; i < n; i++)
        list = node(EN_ARG, T_VOID, args[i], list, 0);
    t = unqual(types[ft].base);
    n = node(EN_CALL, t, list, fp, fsym);
    if (is_struct(t) && type_size(t) == 0)
        error_s("call to a function returning an incomplete struct: ", fname);
    else if (is_struct(t) || is_mem8(t))
        nodes[n].c = new_temp(t);       /* the result object (abi.md 4) */
    return n;
}

/* ---- <stdarg.h>'s built-ins ----------------------------------------------------------
 * stdarg.h's macros map va_start, va_arg and va_end onto these, which walk
 * the 3-byte argument slots (abi.md section 4): the unnamed arguments
 * follow the last named parameter's slot. */

/* __va_start(ap, last), __va_arg(ap, type) or __va_end(ap), the current
 * token being the name. va_list is a char pointer to the next unnamed
 * argument's slot; each built-in becomes ordinary nodes on it. */
static int va_builtin(char *name)
{
    int ap;
    int s;
    int t;
    int p;
    int n;

    next();
    expect(TK_P + '(', "'(' after a va_ built-in");
    ap = assign_expr();
    if (!is_lvalue(ap) || !is_pointer(nodes[ap].type))
        error_s("the first argument must be a va_list variable: ", name);
    if (strcmp(name, "__va_end") == 0) {
        /* nothing to do, but the argument's side effects happen (GCC's
         * va-arg-21.c: va_end(**ap_ptr++)) */
        expect(TK_P + ')', "')'");
        return node(EN_CAST, T_VOID, pure(ap) ? num(0, T_INT) : ap, -1, 0);
    }
    expect(TK_P + ',', "','");
    if (strcmp(name, "__va_start") == 0) {
        s = tok == TK_IDENT ? find_local(tok_name) : -1;
        /* the last named parameter is the one whose slots end where the
         * named parameters' slots end */
        if (!func_variadic)
            error("va_start in a function without '...'");
        else if (s < 0 || !locals[s].param || locals[s].offset + 3 * slots(locals[s].type) != 6 + 3 * nparams_cur)
            error("va_start needs the last named parameter");
        if (tok == TK_IDENT)
            next();
        expect(TK_P + ')', "')'");
        if (s < 0)
            return num(0, T_INT);
        /* ap = (char *)&last + its slots */
        p = node(EN_ADDR, ptr_to(T_CHAR), node(EN_LVAR, locals[s].type, -1, -1, s), -1, 0);
        p = node(EN_BIN, ptr_to(T_CHAR), p, num(3 * slots(locals[s].type), T_INT), B_ADD);
        n = node(EN_ASSIGN, nodes[ap].type, ap, convert(p, nodes[ap].type, "va_start"), 0);
        return node(EN_CAST, T_VOID, n, -1, 0);
    }
    t = parse_type_name();
    expect(TK_P + ')', "')'");
    if (!is_scalar(t) && !(is_struct(t) && type_size(t) > 0)) {
        error("va_arg needs a number, pointer or struct type");
        return num(0, T_INT);
    }
    /* *(t *)((ap += k) - k), k its slots' bytes: a char is the low byte of its slot.
     * The nodes are built directly, not by mk_sub, so no pointer scaling
     * applies: k is a byte count. */
    p = node(EN_ASGOP, nodes[ap].type, ap, num(3 * slots(t), T_INT), B_ADD);
    p = node(EN_BIN, ptr_to(t), p, num(3 * slots(t), T_INT), B_SUB);
    return node(EN_DEREF, t, p, -1, 0);
}

/* ---- GCC's __builtin_ functions -----------------------------------------------------
 * Names with two underscores are the implementation's, so these are there
 * in both modes (c89_spec.md 13). A library function's builtin is that
 * function, declared with its C89 prototype if the program has not
 * declared it; the rest are expressions of their own. The signatures: the
 * result, a colon, the parameters, a '.' for "...": v void, i int, u
 * size_t, l long, p void *, P const void *, c char *, C const char *. */

static char *builtin_sigs[] = {
    "abort v:", "exit v:i", "abs i:i", "labs l:l", "malloc p:u", "calloc p:uu", "free v:p",
    "memcpy p:pPu", "memmove p:pPu", "memset p:piu", "memcmp i:PPu", "strcpy c:cC", "strncpy c:cCu",
    "strcat c:cC", "strncat c:cCu", "strcmp i:CC", "strncmp i:CCu", "strlen u:C", "strchr c:Ci",
    "strrchr c:Ci", "strstr c:CC", "printf i:C.", "sprintf i:cC.", "puts i:C", "putchar i:i", NULL
};

/* the type a signature letter stands for */
static int sig_type(int c)
{
    switch (c) {
    case 'v':
        return T_VOID;
    case 'u':
        return T_UINT;
    case 'l':
        return T_LONG;
    case 'p':
        return ptr_to(T_VOID);
    case 'P':
        return ptr_to(qualify(T_VOID, Q_CONST));
    case 'c':
        return ptr_to(T_CHAR);
    case 'C':
        return ptr_to(qualify(T_CHAR, Q_CONST));
    default:
        return T_INT;
    }
}

/* The global for library function name (declared now if need be), or -1
 * if it has no builtin. */
static int builtin_function(char *name)
{
    int i;
    int n;
    int k;
    int s;
    int ps[8];
    char *sig;

    for (i = 0; builtin_sigs[i] != NULL; i++) {
        sig = builtin_sigs[i];
        n = strlen(name);
        if (strncmp(sig, name, n) != 0 || sig[n] != ' ')
            continue;
        s = find_global(intern(name));
        if (s >= 0)
            return s;
        sig = sig + n + 3;              /* past "name r:" to the parameters */
        k = 0;
        while (*sig && *sig != '.') {
            ps[k] = sig_type(*sig);
            k++;
            sig++;
        }
        return add_global(intern(name), SK_FUNC, func_type(sig_type(builtin_sigs[i][n + 1]), ps, k, *sig == '.'),
                          SC_EXTERN);
    }
    return -1;
}

/* __builtin_offsetof(type, member): a designator of members and constant
 * subscripts, as offsetof takes. Computed here from the member table to an
 * unsigned int (size_t) constant; anything left before the ')' after an
 * error is skipped. */
static int builtin_offsetof(void)
{
    int t;
    int m;
    int off;
    int k;

    expect(TK_P + '(', "'('");
    t = parse_type_name();
    expect(TK_P + ',', "','");
    off = 0;
    for (;;) {
        if (tok != TK_IDENT || !is_struct(t) || !tags[types[t].base].complete) {
            error("__builtin_offsetof needs a member of a complete struct");
            break;
        }
        m = find_member(types[t].base, tok_name);
        if (m < 0) {
            error_s("no member named ", name_str(tok_name));
            break;
        }
        off = off + members[m].offset;
        t = members[m].type;
        next();
        while (accept(TK_P + '[')) {
            k = const_expr();
            expect(TK_P + ']', "']'");
            if (types[t].kind == TY_ARRAY) {
                t = types[t].base;
                off = off + k * type_size(t);
            }
        }
        if (!accept(TK_P + '.'))
            break;
    }
    while (tok != TK_P + ')' && tok != TK_EOF)
        next();
    expect(TK_P + ')', "')'");
    return num(off, T_UINT);
}

/* __builtin_name, the current token: an expression node, or -1 with *s the
 * global to call instead (or -1: no such builtin). In the second case the
 * token is left in place, and primary() goes on as for that function's
 * own name. */
static int gnu_builtin(char *name, int *s)
{
    int n;

    *s = -1;
    if (strcmp(name, "expect") == 0 || strcmp(name, "constant_p") == 0 || strcmp(name, "prefetch") == 0
        || strcmp(name, "offsetof") == 0) {
        next();
        if (strcmp(name, "offsetof") == 0)
            return builtin_offsetof();
        expect(TK_P + '(', "'('");
        n = assign_expr();
        if (strcmp(name, "constant_p") == 0) {
            expect(TK_P + ')', "')'");
            return num(is_const(n), T_INT);             /* never evaluated */
        }
        while (accept(TK_P + ','))
            assign_expr();              /* expect's value, prefetch's options: constants */
        expect(TK_P + ')', "')'");
        return strcmp(name, "expect") == 0 ? n : node(EN_CAST, T_VOID, n, -1, 0);
    }
    if (strcmp(name, "trap") == 0 || strcmp(name, "unreachable") == 0)
        name = "abort";
    *s = builtin_function(name);
    return -1;
}

/* A primary expression: a constant, a string literal, a name, or a
 * parenthesised expression. A name is looked up innermost first: a
 * local (which may stand for a global, a typedef or an enumerator), then
 * a global, then the built-ins. A function name followed by '(' is a
 * direct call, parsed here; otherwise postfix() handles what follows. */
static int primary(void)
{
    int n;
    int s;
    int t;

    if (tok == TK_NUM && is_floating(tok_type)) {
        n = fnum(&tok_fval, tok_type);
        next();
        return n;
    }
    if (tok == TK_NUM) {
        n = node(EN_NUM, tok_type, -1, -1, tok_val);
        nodes[n].hi8 = tok_hi8;
        fbits[n] = tok_fval;            /* a long long's 64 bits */
        next();
        return n;
    }
    if (tok == TK_STR && tok_wide) {
        n = node(EN_GVAR, array_of(T_INT, tok_len + 1), -1, -1, wide_string(tok_str, tok_len));
        next();
        return n;
    }
    if (tok == TK_STR) {
        /* inside a function, a string goes in the function's own pool (an
         * S record); elsewhere it is a static object of its own */
        t = array_of(T_CHAR, tok_len + 1);
        if (in_function && !in_static_init)     /* not in a static object's initialiser */
            n = node(EN_STR, t, -1, -1, func_string(tok_str, tok_len));
        else
            n = node(EN_GVAR, t, -1, -1, file_string(tok_str, tok_len));
        next();
        return n;
    }
    if (tok == TK_IDENT) {
        s = find_local(tok_name);
        if (s >= 0 && locals[s].kind == LK_GLOBAL) {
            s = locals[s].offset;       /* a block-scope extern or static: the global below */
        } else if (s >= 0) {
            if (locals[s].kind == LK_TYPEDEF) {
                error_s("a type name where a value is expected: ", name_str(tok_name));
                next();
                return num(0, T_INT);
            }
            next();
            if (locals[s].kind == LK_ENUMC)
                return num(locals[s].offset, T_INT);
            return node(EN_LVAR, locals[s].type, -1, -1, s);
        } else {
            s = find_global(tok_name);
        }
        if (s < 0 && (strcmp(name_str(tok_name), "__va_start") == 0 || strcmp(name_str(tok_name), "__va_arg") == 0
                      || strcmp(name_str(tok_name), "__va_end") == 0))
            return va_builtin(name_str(tok_name));
        if (s < 0 && strncmp(name_str(tok_name), "__builtin_", 10) == 0) {
            n = gnu_builtin(name_str(tok_name) + 10, &s);
            if (n >= 0)
                return n;
        }
        /* an undeclared name being called: an implicit declaration in
         * strict mode, an error in the default mode (c89_spec.md 1) */
        if (s < 0 && strict && peek() == TK_P + '(') {
            /* C89 3.3.2.2: an implicit "extern int name();" */
            warning_s("implicit declaration of function ", name_str(tok_name));
            s = add_global(tok_name, SK_FUNC, func_type(T_INT, NULL, -1, 0), SC_EXTERN);
            globals[s].implicit = 1;
        }
        if (s < 0) {
            /* reported, then replaced by 0 (a call's arguments skipped)
             * so that parsing can go on */
            if (peek() == TK_P + '(')
                error_s("call to undeclared function ", name_str(tok_name));
            else
                error_s("undeclared identifier ", name_str(tok_name));
            next();
            if (tok == TK_P + '(') {
                next();
                while (tok != TK_P + ')' && tok != TK_EOF)
                    next();
                accept(TK_P + ')');
            }
            return num(0, T_INT);
        }
        if (globals[s].kind == SK_TYPEDEF) {
            error_s("a type name where a value is expected: ", name_str(tok_name));
            next();
            return num(0, T_INT);
        }
        globals[s].used = 1;
        next();
        if (globals[s].kind == SK_ENUMC)
            return num(globals[s].val, T_INT);
        if (globals[s].kind == SK_FUNC && tok == TK_P + '(') {
            next();
            return call(s, globals[s].type, -1);
        }
        return node(EN_GVAR, globals[s].type, -1, -1, s);
    }
    if (accept(TK_P + '(')) {
        n = expression();
        expect(TK_P + ')', "')'");
        return n;
    }
    fatal("expected an expression");
    return 0;
}

/* *p: the object p points to. *&x is simply x, not a DEREF of an ADDR. */
static int deref(int p)
{
    int t;

    t = value_type(p);
    if (!is_pointer(t)) {
        error("'*' or '[]' applied to a non-pointer");
        return p;
    }
    if (is_void(types[t].base))
        error("dereferencing a void pointer");
    if (nodes[p].op == EN_ADDR)
        return nodes[p].a;
    return node(EN_DEREF, types[t].base, p, -1, 0);
}

/* ++ or -- on n, kind as EN_INC's val: 0 ++x, 1 x++, 2 --x, 3 x--. An
 * ordinary lvalue becomes one EN_INC node (cc2 scales a pointer's step by
 * the size emit.c passes); a bit-field is lowered to a store. */
static int mk_inc(int n, int kind)
{
    int pre;
    int v;

    if (nodes[n].op == EN_BITF) {
        /* x++ is (x += 1) - 1, re-extended to the field's width */
        n = bf_stable(n, &pre);
        v = bf_store(n, node(EN_BIN, nodes[n].type, n, num(1, T_INT), kind < 2 ? B_ADD : B_SUB));
        if (kind == 1 || kind == 3)
            v = bf_extend(node(EN_BIN, T_UINT, v, num(1, T_INT), kind == 1 ? B_SUB : B_ADD), nodes[n].c, nodes[n].hi8);
        return with_pre(pre, v);
    }
    if (!is_lvalue(n)) {
        error("'++' or '--' needs an lvalue");
        return n;                       /* (as an assignment does) nothing to emit from it */
    } else if (types[nodes[n].type].qual & Q_CONST)
        error("'++' or '--' of a const object");
    else
        need_scalar(n, "'++'/'--'");
    return node(EN_INC, nodes[n].type, n, -1, kind);
}

/* s.m or p->m: a DEREF of the member's type at the struct's address plus
 * the member's offset. The member's type takes on the struct's
 * qualifiers (a member of a const struct is const). A bit-field gives an
 * EN_BITF node instead. */
static int member(int n, int arrow)
{
    int t;
    int p;
    int m;
    int mt;
    int off;

    next();
    if (tok != TK_IDENT)
        fatal("expected a member name");
    t = value_type(n);
    if (arrow) {
        if (!is_pointer(t) || !is_struct(types[t].base)) {
            error("'->' needs a pointer to a struct");
            next();
            return n;
        }
        t = types[t].base;
        p = n;
    } else {
        t = nodes[n].type;              /* with its qualifiers, which the member inherits */
        if (!is_struct(t)) {
            error("'.' needs a struct");
            next();
            return n;
        }
        if (!is_lvalue(n))
            p = node(EN_CAST, ptr_to(t), n, -1, 0);     /* a struct value is its object's address */
        else
            p = nodes[n].op == EN_DEREF ? nodes[n].a : node(EN_ADDR, ptr_to(t), n, -1, 0);
    }
    if (!tags[types[t].base].complete) {
        error("member of an incomplete struct");
        next();
        return n;
    }
    m = find_member(types[t].base, tok_name);
    if (m < 0) {
        error_s("no member named ", name_str(tok_name));
        next();
        return n;
    }
    next();
    mt = qualify(members[m].type, types[t].qual);
    off = members[m].offset;
    if (members[m].width > 0) {
        /* the field's bytes: one, or (a field wider than 8 bits, which
         * starts a byte) three, read and written whole; for a field of 9
         * to 16 bits the third byte is beyond the field (the next member,
         * or past the struct) and is written back as read. The node's
         * type is int, as a bit-field's value promotes to int, unless a
         * 24-bit unsigned field needs unsigned int to hold it. */
        p = node(EN_BIN, ptr_to(T_UCHAR), node(EN_CAST, ptr_to(T_UCHAR), p, -1, 0), num(off, T_INT), B_ADD);
        if (members[m].width > 8)
            p = node(EN_CAST, ptr_to(T_UINT), p, -1, 0);
        p = node(EN_BITF, members[m].width < 24 || !is_unsigned(mt) ? T_INT : T_UINT, p, -1, members[m].bit);
        nodes[p].c = members[m].width;
        nodes[p].hi8 = !is_unsigned(mt);
        return p;
    }
    if (is_const(p))
        p = num(nodes[p].val + off, ptr_to(mt));        /* ((T *)0)->m: offsetof */
    else if (off != 0)
        p = node(EN_BIN, ptr_to(mt), p, num(off, T_INT), B_ADD);
    else
        p = node(EN_CAST, ptr_to(mt), p, -1, 0);
    return node(EN_DEREF, mt, p, -1, 0);
}

/* A primary followed by any number of [] () . -> ++ --, applied left to
 * right in a loop. */
static int postfix(void)
{
    int n;
    int i;
    int t;

    n = primary();
    for (;;) {
        if (accept(TK_P + '[')) {
            i = expression();
            expect(TK_P + ']', "']'");
            n = deref(mk_add(n, i));    /* a[i] is *(a + i), by definition (C89 3.3.2.1) */
        } else if (accept(P_INC)) {
            n = mk_inc(n, 1);
        } else if (accept(P_DEC)) {
            n = mk_inc(n, 3);
        } else if (tok == TK_P + '(') {
            /* a call through a pointer to a function (or a function
             * designator, which is one after decay) */
            t = value_type(n);
            next();
            if (!is_pointer(t) || types[types[t].base].kind != TY_FUNC) {
                error("a call to something that is not a function");
                while (tok != TK_P + ')' && tok != TK_EOF)
                    next();
                accept(TK_P + ')');
            } else if (nodes[n].op == EN_GVAR && types[nodes[n].type].kind == TY_FUNC) {
                n = call(nodes[n].val, types[t].base, -1);     /* (f)(x): a direct call */
            } else {
                n = call(-1, types[t].base, n);
            }
        } else if (tok == TK_P + '.' || tok == P_ARROW) {
            n = member(n, tok == P_ARROW);
        } else {
            return n;
        }
    }
}

/* ---- unary and cast ------------------------------------------------------------- */

static int cast_expr(void);

/* sizeof t, a size_t (unsigned int) constant */
static int sizeof_value(int t)
{
    if (types[t].kind == TY_VOID || types[t].kind == TY_FUNC || type_size(t) == 0)
        error("sizeof applied to void, a function or an incomplete type");
    return num(type_size(t), T_UINT);
}

static int unary(void);

/* sizeof's operand, which is not evaluated (C89 3.3.3.4): its type, with
 * nothing it made kept - its nodes, a call's result temporary, or the marks
 * that bring the floating-point and long long conversions into a program. */
static int sizeof_operand(void)
{
    int mark;
    int nloc;
    int fp;
    int ll;
    int n;
    int t;

    mark = nnodes;
    nloc = nlocals;
    fp = fp_used;
    ll = ll_used;
    n = unary();
    t = nodes[n].type;                  /* not decayed: sizeof an array is the array's size */
    if (nodes[n].op == EN_BITF)
        error("sizeof a bit-field");
    nnodes = mark;
    nlocals = nloc;
    fp_used = fp;
    ll_used = ll;
    return t;
}

/* The prefix operators ++ -- & * + - ~ ! and sizeof, each applied to a
 * cast expression (sizeof and ++/-- to a unary one), as C89 3.3.3 has
 * them; constants fold. */
static int unary(void)
{
    int n;
    int t;
    struct i32 v;
    struct i64 q;

    if (accept(P_INC))
        return mk_inc(unary(), 0);
    if (accept(P_DEC))
        return mk_inc(unary(), 2);
    if (accept(TK_P + '&')) {
        n = cast_expr();
        if (nodes[n].op == EN_BITF)
            error("the address of a bit-field");
        /* &*p is p, which also makes &a[i] the address arithmetic alone */
        if (nodes[n].op == EN_DEREF)
            return nodes[n].a;
        if (nodes[n].op != EN_GVAR && nodes[n].op != EN_LVAR && nodes[n].op != EN_STR) {
            error("'&' needs an lvalue");
            return n;
        }
        return node(EN_ADDR, ptr_to(nodes[n].type), n, -1, 0);
    }
    if (accept(TK_P + '*'))
        return deref(cast_expr());
    if (accept(TK_P + '+')) {
        n = cast_expr();
        need_arith(n, "unary '+'");
        return conv(n, promote(value_type(n)));
    }
    if (accept(TK_P + '-')) {
        n = cast_expr();
        need_arith(n, "unary '-'");
        t = promote(value_type(n));
        n = conv(n, t);
        /* IEEE negation is a flip of the sign bit, NaNs and zeros too
         * (-0.0 is a constant of its own) */
        if (is_const(n) && is_floating(t)) {
            if (types[t].kind == TY_FLOAT)
                fbits[n].lo = fbits[n].lo ^ 0x80000000UL;
            else
                fbits[n].hi = fbits[n].hi ^ 0x80000000UL;
            return n;
        }
        if (is_const(n) && is_llong(t)) {
            qval(n, &q);
            i64_neg(&q, &q);
            return qnum(&q, t);
        }
        if (is_const(n)) {
            cval(n, &v);
            i32_neg(&v, &v);
            return num32(&v, t);
        }
        return node(EN_NEG, t, n, -1, 0);
    }
    if (accept(TK_P + '~')) {
        n = cast_expr();
        need_integer(n, "'~'");
        t = promote(value_type(n));
        n = conv(n, t);
        if (is_const(n) && is_llong(t)) {
            qval(n, &q);
            i64_cpl(&q, &q);
            return qnum(&q, t);
        }
        if (is_const(n)) {
            cval(n, &v);
            i32_cpl(&v, &v);
            return num32(&v, t);
        }
        return node(EN_CPL, t, n, -1, 0);
    }
    if (accept(TK_P + '!')) {
        n = cast_expr();
        need_scalar(n, "'!'");
        if (is_const(n))
            return num(!const_nonzero(n), T_INT);
        if (is_floating(value_type(n)))
            return bin(B_EQ, T_INT, n, fzero(value_type(n)));
        if (is_llong(value_type(n)))
            return bin(B_EQ, T_INT, n, qzero(value_type(n)));
        return node(EN_NOT, T_INT, n, -1, 0);
    }
    if (accept(KW_SIZEOF)) {
        /* "sizeof (" starts a type name only if a type follows the "(";
         * otherwise it is a parenthesised expression */
        if (tok == TK_P + '(' && is_type_start_after_paren()) {
            next();
            t = parse_type_name();
            expect(TK_P + ')', "')'");
            return sizeof_value(t);
        }
        return sizeof_value(sizeof_operand());
    }
    return postfix();
}

/* (type) cast-expression, or a unary expression. A "(" is a cast only if
 * the token after it starts a type name, which is where the lexer's
 * one-token peek is needed. A cast of a constant stays a constant. */
static int cast_expr(void)
{
    int t;
    int n;
    int s;

    if (tok == TK_P + '(' && is_type_start_after_paren()) {
        next();
        t = parse_type_name();
        expect(TK_P + ')', "')'");
        n = cast_expr();
        s = value_type(n);
        if (types[t].kind == TY_VOID)
            return node(EN_CAST, T_VOID, n, -1, 0);
        if (!is_scalar(t) || !is_scalar(s)) {
            error("casts are only between numbers and pointers");
            return n;
        }
        if ((is_floating(t) && is_pointer(s)) || (is_pointer(t) && is_floating(s))) {
            error("a cast between a pointer and a floating type");
            return n;
        }
        t = unqual(t);
        if (is_const(n) && is_arith(t) && is_arith(s))
            return conv(n, t);
        if (is_const(n) && (is_pointer(t) || is_pointer(s)) && !is_long(s) && type_size(t) == 3)
            return num(nodes[n].val, t);        /* a constant still: offsetof's &((T *)0)->m */
        return node(EN_CAST, t, n, -1, 0);
    }
    return unary();
}

/* ---- binary operators by precedence --------------------------------------------- */

/* A binary operator's precedence, 10 binding tightest; 0 for a token that
 * is not a binary operator (which ends the expression at this level). */
static int prec(int tk)
{
    switch (tk) {
    case TK_P + '*': case TK_P + '/': case TK_P + '%': return 10;
    case TK_P + '+': case TK_P + '-': return 9;
    case P_SHL: case P_SHR: return 8;
    case TK_P + '<': case TK_P + '>': case P_LE: case P_GE: return 7;
    case P_EQ: case P_NE: return 6;
    case TK_P + '&': return 5;
    case TK_P + '^': return 4;
    case TK_P + '|': return 3;
    case P_ANDAND: return 2;
    case P_OROR: return 1;
    }
    return 0;
}

/* Precedence climbing: one function parses every binary operator level,
 * instead of one function per level. binary(level) reads an operand,
 * then, while the next operator binds at least as tightly as level,
 * takes it and parses its right operand with binary(prec + 1), so only
 * tighter operators go into the right operand. Asking for one more than
 * the operator's own precedence makes operators of equal precedence group
 * to the left: a - b - c is (a - b) - c. */
static int binary(int level)
{
    int a;
    int b;
    int tk;

    a = cast_expr();
    for (;;) {
        tk = tok;
        if (prec(tk) < level || prec(tk) == 0)
            return a;
        next();
        b = binary(prec(tk) + 1);
        switch (tk) {
        case TK_P + '+':
            a = mk_add(a, b);
            break;
        case TK_P + '-':
            a = mk_sub(a, b);
            break;
        case TK_P + '<': case TK_P + '>': case P_LE: case P_GE: case P_EQ: case P_NE:
            a = mk_compare(tk, a, b);
            break;
        case P_ANDAND:
        case P_OROR:
            /* folded only when both sides are constants; otherwise the
             * node keeps the short-circuit evaluation (LAND, LOR) */
            need_scalar(a, "'&&'/'||'");
            need_scalar(b, "'&&'/'||'");
            a = truth(a);
            b = truth(b);
            if (is_const(a) && is_const(b))
                a = num(tk == P_ANDAND ? (const_nonzero(a) && const_nonzero(b))
                        : (const_nonzero(a) || const_nonzero(b)), T_INT);
            else
                a = node(tk == P_ANDAND ? EN_LAND : EN_LOR, T_INT, a, b, 0);
            break;
        default:
            a = mk_arith(tk, a, b);
            break;
        }
    }
}

/* c ? a : b (C89 3.3.15). The middle operand may be any expression, commas
 * included; the last is another conditional, so ?: groups to the right.
 * The result type follows the operands: their common arithmetic type,
 * the pointer type beside a null pointer constant, a pointer combining
 * both sides' qualifiers, or the same struct or void on both sides. */
static int conditional(void)
{
    int c;
    int a;
    int b;
    int ta;
    int tb;
    int t;
    int n;

    c = binary(1);
    if (!accept(TK_P + '?'))
        return c;
    need_scalar(c, "'?:'");
    c = truth(c);
    a = expression();
    expect(TK_P + ':', "':' in '?:'");
    b = conditional();
    ta = value_type(a);
    tb = value_type(b);
    if (is_arith(ta) && is_arith(tb)) {
        t = arith_type(a, b);
        a = conv(a, t);
        b = conv(b, t);
    } else if (is_pointer(ta) && is_null(b))
        t = ta;
    else if (is_pointer(tb) && is_null(a))
        t = tb;
    else if (is_pointer(ta) && is_pointer(tb)
             && (unqual(types[ta].base) == unqual(types[tb].base) || is_void(types[ta].base)
                 || is_void(types[tb].base))) {
        /* C89 3.3.15: a pointer to the (void, if either is) type with both
         * sides' qualifiers */
        t = is_void(types[ta].base) ? types[ta].base : is_void(types[tb].base) ? types[tb].base : types[ta].base;
        t = ptr_to(qualify(t, types[types[ta].base].qual | types[types[tb].base].qual));
    } else if (is_struct(ta) && ta == tb) {
        t = ta;                         /* a struct value: SEL of two addresses */
    } else if (ta == T_VOID && tb == T_VOID)
        t = T_VOID;
    else {
        error("'?:' branches have incompatible types");
        t = ta;
    }
    if (is_const(c) && is_const(a) && is_const(b))
        return const_nonzero(c) ? a : b;
    n = node(EN_COND, t, c, a, 0);
    nodes[n].c = b;
    return n;
}

/* ---- assignment ----------------------------------------------------------------- */

/* An lvalue whose address can be computed twice with the same result and
 * no side effect: a variable, or *p for a pointer variable p. */
static int is_simple_lvalue(int n)
{
    int op;

    op = nodes[n].op;
    if (op == EN_GVAR || op == EN_LVAR)
        return 1;
    return op == EN_DEREF && (nodes[nodes[n].a].op == EN_GVAR || nodes[nodes[n].a].op == EN_LVAR);
}

/* the binary operator token of a compound assignment token (+= gives +),
 * or 0 */
static int asg_op(int tk)
{
    switch (tk) {
    case P_MULEQ: return TK_P + '*';
    case P_DIVEQ: return TK_P + '/';
    case P_MODEQ: return TK_P + '%';
    case P_ADDEQ: return TK_P + '+';
    case P_SUBEQ: return TK_P + '-';
    case P_SHLEQ: return P_SHL;
    case P_SHREQ: return P_SHR;
    case P_ANDEQ: return TK_P + '&';
    case P_XOREQ: return TK_P + '^';
    case P_OREQ: return TK_P + '|';
    }
    return 0;
}

/* An assignment expression: a conditional, or lvalue = or op= another
 * assignment expression (so a = b = c groups to the right). Plain = is
 * one EN_ASSIGN. a op= b evaluates a's address once, which C requires
 * (a[i++] += 1 increments i once): the common case is one EN_ASGOP node,
 * whose IR record (ASG) computes in a's own type. When the operation's
 * type differs from a's in a way that changes the result (a float
 * += a double, a char /= a long) it is rewritten into a = (T)(a op b),
 * through a temporary pointer when a's address is not simple; a
 * bit-field becomes a bf_store. */
int assign_expr(void)
{
    int a;
    int b;
    int tk;
    int op;
    int t;
    int r;
    int tmp;
    int p;
    int d;
    int pre;

    a = conditional();
    tk = tok;
    if (tk != TK_P + '=' && asg_op(tk) == 0)
        return a;
    next();
    b = assign_expr();
    if (!is_lvalue(a)) {
        error("assignment to something that is not an lvalue");
        return a;
    }
    t = nodes[a].type;
    if (types[t].qual & Q_CONST)
        error("assignment to a const object");
    t = unqual(t);
    if (is_struct(t) && type_size(t) == 0)
        error("assignment of an incomplete struct");
    if (nodes[a].op == EN_BITF) {
        /* the value to store, worked out first (for op=, from the field's
         * current value), then bf_store; a long value is cut to 24 bits,
         * which a field never exceeds */
        a = bf_stable(a, &pre);
        if (is_floating(value_type(b)))
            b = conv(b, is_unsigned(nodes[a].type) ? T_UINT : T_INT);
        need_integer(b, "assignment to a bit-field");
        if (tk != TK_P + '=')
            b = asg_op(tk) == TK_P + '+' ? mk_add(a, b) : asg_op(tk) == TK_P + '-' ? mk_sub(a, b) : mk_arith(asg_op(tk), a, b);
        return with_pre(pre, bf_store(a, conv(b, is_long(value_type(b)) || is_llong(value_type(b)) ? T_UINT
                                                  : value_type(b))));
    }
    if (tk == TK_P + '=')
        return node(EN_ASSIGN, t, a, convert(b, t, "assignment"), 0);
    /* a op= b: the operator from the types of a and b, as for a op b */
    op = asg_op(tk);
    if (is_pointer(t)) {
        /* p += n: n scaled by the element size, as for p + n */
        if (op != TK_P + '+' && op != TK_P + '-')
            error("only += and -= apply to a pointer");
        need_integer(b, "pointer '+='/'-='");
        return node(EN_ASGOP, t, a, scale(b, t), op == TK_P + '+' ? B_ADD : B_SUB);
    }
    if (is_floating(t) || is_floating(value_type(b))) {
        /* + - * / with a floating side: in the type of a op b, then (if
         * that is not a's) converted back, the address computed once */
        if (op != TK_P + '+' && op != TK_P + '-' && op != TK_P + '*' && op != TK_P + '/') {
            error("only += -= *= /= apply to a floating value");
            return a;
        }
        need_arith(a, "compound assignment");
        need_arith(b, "compound assignment");
        if (arith_type(a, b) == t)
            return node(EN_ASGOP, t, a, conv(b, t), op == TK_P + '+' ? B_ADD : op == TK_P + '-' ? B_SUB
                        : op == TK_P + '*' ? B_MUL : B_DIVS);
        if (is_simple_lvalue(a)) {
            r = op == TK_P + '+' ? mk_add(a, b) : op == TK_P + '-' ? mk_sub(a, b) : mk_arith(op, a, b);
            return node(EN_ASSIGN, t, a, conv(r, t), 0);
        }
        tmp = new_temp(ptr_to(t));
        p = node(EN_ASSIGN, ptr_to(t), node(EN_LVAR, ptr_to(t), -1, -1, tmp), node(EN_ADDR, ptr_to(t), a, -1, 0), 0);
        d = node(EN_DEREF, t, node(EN_LVAR, ptr_to(t), -1, -1, tmp), -1, 0);
        r = op == TK_P + '+' ? mk_add(d, b) : op == TK_P + '-' ? mk_sub(d, b) : mk_arith(op, d, b);
        return node(EN_COMMA, t, p, node(EN_ASSIGN, t, d, conv(r, t), 0), 0);
    }
    need_integer(a, "compound assignment");
    need_integer(b, "compound assignment");
    if (arith_type(a, b) != promote(t) && (is_long(arith_type(a, b)) || is_llong(arith_type(a, b))) && op != P_SHL
        && op != P_SHR) {
        /* a narrower lvalue op= a long or long long: computed in the wider
         * type, as a = (T)(a op b) */
        if (op == TK_P + '/' || op == TK_P + '%') {
            if (is_simple_lvalue(a)) {
                r = mk_arith(op, a, b);
                return node(EN_ASSIGN, t, a, conv(r, t), 0);
            }
            /* the object's address once, in a temporary: (tmp = &a, *tmp = *tmp op b) */
            tmp = new_temp(ptr_to(t));
            p = node(EN_ASSIGN, ptr_to(t), node(EN_LVAR, ptr_to(t), -1, -1, tmp), node(EN_ADDR, ptr_to(t), a, -1, 0), 0);
            d = node(EN_DEREF, t, node(EN_LVAR, ptr_to(t), -1, -1, tmp), -1, 0);
            r = mk_arith(op, d, b);
            return node(EN_COMMA, t, p, node(EN_ASSIGN, t, d, conv(r, t), 0), 0);
        }
        /* + - * & | ^ give the same low bits computed in the narrower type */
        b = conv(b, promote(t));
    }
    /* build a op b as usual, then turn that EN_BIN node into the EN_ASGOP:
     * its left operand is still the lvalue itself, since converting an
     * integer lvalue to the operation's type here needs no node */
    r = op == TK_P + '+' ? mk_add(a, b) : op == TK_P + '-' ? mk_sub(a, b) : mk_arith(op, a, b);
    if (nodes[r].op == EN_NUM) {
        /* folded (a is constant?) - cannot happen for an lvalue */
        return r;
    }
    op = nodes[r].val;
    nodes[r].op = EN_ASGOP;
    nodes[r].type = t;
    nodes[r].val = op;
    return r;
}

/* An expression: assignment expressions separated by commas, each
 * evaluated in turn; the value and type are the last one's. */
int expression(void)
{
    int a;
    int b;

    a = assign_expr();
    while (accept(TK_P + ',')) {
        b = assign_expr();
        a = node(EN_COMMA, nodes[b].type, a, b, 0);
    }
    return a;
}

/* ---- constant expressions -------------------------------------------------------- */

/* An integer constant expression (C89 3.4): grammatically a conditional
 * expression, so no assignment or comma at its top level. The parser
 * folds as it goes, so a constant expression is simply one whose tree
 * came out as a single integer EN_NUM. Its nodes are discarded. */

/* the value's 32 bits (a long long's low 32) and its type, for a case
 * label, whose value may need more than an int */
void const_expr32(struct i32 *r, int *type)
{
    int n;
    int mark;

    mark = nnodes;
    n = conditional();
    if (!is_const(n) || !is_integer(nodes[n].type)) {
        error("not an integer constant expression");
        i32_set(r, 0, 0);
        *type = T_INT;
    } else {
        cval(n, r);
        *type = nodes[n].type;
    }
    nnodes = mark;
}

/* A case of a switch on a long long (stmt.c): its constant expression,
 * converted to the switch's type t, to *v; returns the node that tests
 * local tmp, which holds the switch's value, against it. */
int case_test64(int tmp, int t, struct i64 *v)
{
    int n;

    n = conditional();
    if (!is_const(n) || !is_integer(nodes[n].type)) {
        error("not an integer constant expression");
        n = qzero(t);
    }
    n = conv(n, t);
    qval(n, v);
    return bin(B_EQ, T_INT, node(EN_LVAR, t, -1, -1, tmp), n);
}

/* the value's low 24 bits as an int: array sizes, bit-field widths,
 * enumerators, subscripts in offsetof */
int const_expr(void)
{
    int n;
    int mark;
    int v;

    mark = nnodes;
    n = conditional();
    if (!is_const(n) || !is_integer(nodes[n].type)) {
        error("not an integer constant expression");
        v = 0;
    } else {
        v = nodes[n].val;
    }
    nnodes = mark;
    return v;
}
