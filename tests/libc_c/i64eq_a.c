/* i64eq_a.c - int64.c with I64_ASM 1 and every function renamed a_..., for
 * t_i64eq.c to compare with the other build. */

#define I64_ASM 1
#define i64_set a_i64_set
#define i64_from_long a_i64_from_long
#define i64_is_zero a_i64_is_zero
#define i64_is_neg a_i64_is_neg
#define i64_add a_i64_add
#define i64_sub a_i64_sub
#define i64_mul a_i64_mul
#define i64_divu a_i64_divu
#define i64_divs a_i64_divs
#define i64_and a_i64_and
#define i64_or a_i64_or
#define i64_xor a_i64_xor
#define i64_neg a_i64_neg
#define i64_cpl a_i64_cpl
#define i64_shl a_i64_shl
#define i64_shr a_i64_shr
#define i64_cmp a_i64_cmp
#define i64_divsmall a_i64_divsmall
#define i64_muladd a_i64_muladd
#define i64_parse a_i64_parse
#define i64_str a_i64_str
#include "../../src/common/int64.c"
