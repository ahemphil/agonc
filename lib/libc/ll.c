/* ll.c - the helpers compiled code calls for long long (abi.md 6), and
 * the library's long long functions (strtoll ... lldiv, and the printf
 * and scanf family's ll conversions).
 *
 * The arithmetic is int64.c's, the same code cc1 folds constants with. A
 * long long is handled as a double is: by the address of its 8 bytes,
 * which are a struct i64. As in fp.c, cc2 pushes each operand as soon as
 * it is made, so the parameters are in the reverse of the operands'
 * order: __qsub(r, b, a) is a - b. A result goes to an object the caller
 * supplies (r), whose address is returned. The conversions to and from
 * float and double are fp.c's.
 *
 * Where it sits: cc2 turns every long long operation into a call to one
 * of the __q... functions here (abi.md section 6), ordinary C functions
 * called by the C convention (abi.md section 4); comparisons return an
 * int 0 or 1. A long long is 8 bytes and never sits in registers, which
 * hold at most 4 bytes of a value (E:UHL). Part of the library libm.s,
 * as fp.c is.
 *
 * Sections: the operators (with the shared divide, binary and shift
 * that the compound assignments reuse); the digit loops behind printf's
 * and scanf's ll conversions; and <stdlib.h>'s strtoll family.
 */

/* int64's functions under reserved names: a program may use the plain ones.
 * The defines rename them before int64.c is included below, so its code
 * is compiled here as part of this unit. */
#define i64_set __i64_set
#define i64_from_long __i64_from_long
#define i64_is_zero __i64_is_zero
#define i64_is_neg __i64_is_neg
#define i64_add __i64_add
#define i64_sub __i64_sub
#define i64_mul __i64_mul
#define i64_divu __i64_divu
#define i64_divs __i64_divs
#define i64_and __i64_and
#define i64_or __i64_or
#define i64_xor __i64_xor
#define i64_neg __i64_neg
#define i64_cpl __i64_cpl
#define i64_shl __i64_shl
#define i64_shr __i64_shr
#define i64_cmp __i64_cmp
#define i64_divsmall __i64_divsmall
#define i64_muladd __i64_muladd
#define i64_parse __i64_parse
#define i64_str __i64_str

#include "../../src/common/int64.c"
#include <stdlib.h>
#include <errno.h>

/* the operator codes ASG passes (cc2's B_ADD ... B_XOR); 12, B_XOR, has
 * no name here: binary takes it as any code above OP_REMU not handled
 * before */
#define OP_ADD 0
#define OP_SUB 1
#define OP_MUL 2
#define OP_DIVS 3
#define OP_DIVU 4
#define OP_REMS 5
#define OP_REMU 6
#define OP_SHL 7
#define OP_SHRS 8
#define OP_SHRU 9
#define OP_AND 10
#define OP_OR 11

/* ---- the operators ------------------------------------------------------------ */

/* a / b and a % b; by zero, which C leaves undefined, a quotient of 0 and
 * a remainder of a (and no fault). int64's divisions need b != 0; the
 * signed one truncates toward zero, as C89's / does. */
static void divide(struct i64 *q, struct i64 *rem, const struct i64 *a, const struct i64 *b, int is_signed)
{
    if (i64_is_zero(b)) {
        i64_set(q, 0, 0);
        *rem = *a;
    } else if (is_signed) {
        i64_divs(q, rem, a, b);
    } else {
        i64_divu(q, rem, a, b);
    }
}

/* r = a op b for the operators with a long long b: the division helpers
 * and __qasg. A division's quotient and remainder go to locals first, so
 * r may be the same object as a or b there. Shift codes (7 to 9) never
 * come here: the shifts take an int count, through shift. */
static struct i64 *binary(int op, struct i64 *r, const struct i64 *b, const struct i64 *a)
{
    struct i64 q;
    struct i64 rem;

    switch (op) {
    case OP_ADD: i64_add(r, a, b); break;
    case OP_SUB: i64_sub(r, a, b); break;
    case OP_MUL: i64_mul(r, a, b); break;
    case OP_AND: i64_and(r, a, b); break;
    case OP_OR: i64_or(r, a, b); break;
    default:
        if (op > OP_REMU) {
            i64_xor(r, a, b);
            break;
        }
        divide(&q, &rem, a, b, op == OP_DIVS || op == OP_REMS);
        *r = op == OP_DIVS || op == OP_DIVU ? q : rem;
        break;
    }
    return r;
}

/* r = a shifted by n, which C defines for 0 .. 63; other counts are
 * taken mod 64, as int64's shifts require 0 <= n < 64 */
static struct i64 *shift(int op, struct i64 *r, int n, const struct i64 *a)
{
    n = n & 63;
    if (op == OP_SHL)
        i64_shl(r, a, n);
    else
        i64_shr(r, a, n, op == OP_SHRS);
    return r;
}

/* The helpers proper, one per operator, each named in abi.md section 6;
 * the operands are reversed as the header says. == and != compare the
 * halves directly; the orderings go through i64_cmp (-1, 0 or 1),
 * signed or not. */
struct i64 *__qadd(struct i64 *r, const struct i64 *b, const struct i64 *a)
{
    i64_add(r, a, b);
    return r;
}

struct i64 *__qsub(struct i64 *r, const struct i64 *b, const struct i64 *a)
{
    i64_sub(r, a, b);
    return r;
}

struct i64 *__qmul(struct i64 *r, const struct i64 *b, const struct i64 *a)
{
    i64_mul(r, a, b);
    return r;
}

struct i64 *__qdivs(struct i64 *r, const struct i64 *b, const struct i64 *a)
{
    return binary(OP_DIVS, r, b, a);
}

struct i64 *__qdivu(struct i64 *r, const struct i64 *b, const struct i64 *a)
{
    return binary(OP_DIVU, r, b, a);
}

struct i64 *__qrems(struct i64 *r, const struct i64 *b, const struct i64 *a)
{
    return binary(OP_REMS, r, b, a);
}

struct i64 *__qremu(struct i64 *r, const struct i64 *b, const struct i64 *a)
{
    return binary(OP_REMU, r, b, a);
}

struct i64 *__qand(struct i64 *r, const struct i64 *b, const struct i64 *a)
{
    i64_and(r, a, b);
    return r;
}

struct i64 *__qor(struct i64 *r, const struct i64 *b, const struct i64 *a)
{
    i64_or(r, a, b);
    return r;
}

struct i64 *__qxor(struct i64 *r, const struct i64 *b, const struct i64 *a)
{
    i64_xor(r, a, b);
    return r;
}

struct i64 *__qshl(struct i64 *r, int n, const struct i64 *a)
{
    return shift(OP_SHL, r, n, a);
}

struct i64 *__qshrs(struct i64 *r, int n, const struct i64 *a)
{
    return shift(OP_SHRS, r, n, a);
}

struct i64 *__qshru(struct i64 *r, int n, const struct i64 *a)
{
    return shift(OP_SHRU, r, n, a);
}

int __qeq(const struct i64 *b, const struct i64 *a)
{
    return a->lo == b->lo && a->hi == b->hi;
}

int __qne(const struct i64 *b, const struct i64 *a)
{
    return a->lo != b->lo || a->hi != b->hi;
}

int __qlts(const struct i64 *b, const struct i64 *a)
{
    return i64_cmp(a, b, 1) < 0;
}

int __qltu(const struct i64 *b, const struct i64 *a)
{
    return i64_cmp(a, b, 0) < 0;
}

int __qles(const struct i64 *b, const struct i64 *a)
{
    return i64_cmp(a, b, 1) <= 0;
}

int __qleu(const struct i64 *b, const struct i64 *a)
{
    return i64_cmp(a, b, 0) <= 0;
}

int __qgts(const struct i64 *b, const struct i64 *a)
{
    return i64_cmp(a, b, 1) > 0;
}

int __qgtu(const struct i64 *b, const struct i64 *a)
{
    return i64_cmp(a, b, 0) > 0;
}

int __qges(const struct i64 *b, const struct i64 *a)
{
    return i64_cmp(a, b, 1) >= 0;
}

int __qgeu(const struct i64 *b, const struct i64 *a)
{
    return i64_cmp(a, b, 0) >= 0;
}

struct i64 *__qneg(struct i64 *r, const struct i64 *a)
{
    i64_neg(r, a);
    return r;
}

struct i64 *__qcpl(struct i64 *r, const struct i64 *a)
{
    i64_cpl(r, a);
    return r;
}

/* *p op= b; p (the stored object is the expression's value). op is any
 * binary operator but a shift; p is both the result and the left
 * operand, which relies on int64's functions accepting that. */
struct i64 *__qasg(int op, const struct i64 *b, struct i64 *p)
{
    return binary(op, p, b, p);
}

/* *p <<= n, *p >>= n; p */
struct i64 *__qasgsh(int op, int n, struct i64 *p)
{
    return shift(op, p, n, p);
}

/* 1 and -1 as { lo, hi }, for ++ and -- (d is 1 or -1) */
static struct i64 one = { 1, 0 };
static struct i64 minus_one = { 0xFFFFFFFFUL, 0xFFFFFFFFUL };

struct i64 *__qincpre(int d, struct i64 *p)
{
    i64_add(p, p, d > 0 ? &one : &minus_one);
    return p;
}

/* the old value to r */
struct i64 *__qincpost(struct i64 *r, int d, struct i64 *p)
{
    *r = *p;
    i64_add(p, p, d > 0 ? &one : &minus_one);
    return r;
}

/* long and unsigned long widened to long long; an int widens through
 * long first (abi.md section 6) */
struct i64 *__ltoq(struct i64 *r, long v)
{
    i64_from_long(r, (unsigned long)v, 1);
    return r;
}

struct i64 *__ultoq(struct i64 *r, unsigned long v)
{
    i64_from_long(r, v, 0);
    return r;
}

/* ---- the printf and scanf families' ll conversions (stdio.c) ------------------ */

/* The digits of v's magnitude in base, least significant first, from
 * set; returns how many (none for 0). Repeated division by the base,
 * each remainder a digit (i64_divsmall divides unsigned, by up to
 * 65535). The most negative value negates to itself, but read unsigned
 * that is its magnitude, 2^63, so it prints correctly. stdio.c reaches
 * this and __ll_scan through weak references, so they are linked only
 * into programs that use long long. */
int __ll_print(char *digits, const struct i64 *v, int base, const char *set, int is_signed)
{
    struct i64 u;
    int n;

    u = *v;
    if (is_signed && i64_is_neg(&u))
        i64_neg(&u, &u);
    n = 0;
    while (!i64_is_zero(&u)) {
        digits[n] = set[i64_divsmall(&u, &u, (unsigned int)base)];
        n++;
    }
    return n;
}

/* One step of reading an integer: *q = *q * base + digit, modulo 2^64;
 * base 0 negates *q (scanf's last step after a '-'). */
void __ll_scan(struct i64 *q, int base, int digit)
{
    if (base == 0)
        i64_neg(q, q);
    else
        i64_muladd(q, (unsigned int)base, (unsigned int)digit);
}

/* ---- stdlib.h: C99's long long functions -------------------------------------- */

/* A character's digit value in bases up to 36 (letters in either case
 * from 10), or 99, which is no digit in any base. */
static int digit_value(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 10;
    return 99;
}

/* The common part of strtoll and strtoull (C99 7.20.1.4), as stdlib.c's
 * scan is of strtol and strtoul: white space, a sign (to *neg), a 0x or
 * 0X prefix for base 16 or 0, then digits. The magnitude is returned;
 * the callers apply the sign and the range. *overflow is set once the
 * magnitude passes 2^64 - 1, and the digits are still consumed after
 * it. With no digits at all, *endptr is s itself and the value 0. */
static unsigned long long scan_ll(const char *s, char **endptr, int base, int *neg, int *overflow)
{
    const char *p;
    struct i64 v;
    int d;
    int any;

    p = s;
    while (*p == ' ' || (*p >= 9 && *p <= 13))
        p++;
    *neg = 0;
    if (*p == '-') {
        *neg = 1;
        p++;
    } else if (*p == '+') {
        p++;
    }
    /* "0x" counts as a prefix only with a hex digit after it; otherwise
     * the 0 is the number and the x ends it */
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && digit_value(p[2]) < 16) {
        p = p + 2;
        base = 16;
    } else if (base == 0) {
        base = p[0] == '0' ? 8 : 10;
    }
    i64_set(&v, 0, 0);
    any = 0;
    *overflow = 0;
    if (base >= 2 && base <= 36) {
        for (;;) {
            d = digit_value(*p);
            if (d >= base)
                break;
            if (!*overflow && i64_muladd(&v, (unsigned int)base, (unsigned int)d))
                *overflow = 1;
            any = 1;
            p++;
        }
    }
    if (endptr != NULL)
        *endptr = (char *)(any ? p : s);
    if (!any)
        i64_set(&v, 0, 0);
    /* a struct i64 has the target's unsigned long long layout (int64.h) */
    return *(unsigned long long *)&v;
}

/* Out of range: LLONG_MAX or LLONG_MIN, with errno ERANGE. A negative
 * magnitude may reach LLONG_MAX + 1, which is LLONG_MIN; 0ULL - v negates
 * in unsigned arithmetic, where it cannot overflow. */
long long strtoll(const char *s, char **endptr, int base)
{
    unsigned long long v;
    int neg;
    int overflow;

    v = scan_ll(s, endptr, base, &neg, &overflow);
    if (!overflow && (neg ? v <= (unsigned long long)LLONG_MAX + 1 : v <= (unsigned long long)LLONG_MAX))
        return neg ? (long long)(0ULL - v) : (long long)v;
    errno = ERANGE;
    return neg ? LLONG_MIN : LLONG_MAX;
}

/* Out of range: ULLONG_MAX, with errno ERANGE; a '-' negates, as C says. */
unsigned long long strtoull(const char *s, char **endptr, int base)
{
    unsigned long long v;
    int neg;
    int overflow;

    v = scan_ll(s, endptr, base, &neg, &overflow);
    if (overflow) {
        errno = ERANGE;
        return ULLONG_MAX;
    }
    return neg ? 0ULL - v : v;
}

long long atoll(const char *s)
{
    return strtoll(s, NULL, 10);
}

/* LLONG_MIN has no positive counterpart: its llabs is undefined in C
 * (here it stays LLONG_MIN). */
long long llabs(long long n)
{
    return n < 0 ? -n : n;
}

/* / and % here compile to calls of __qdivs and __qrems above */
lldiv_t lldiv(long long numer, long long denom)
{
    lldiv_t r;

    r.quot = numer / denom;
    r.rem = numer % denom;
    return r;
}
