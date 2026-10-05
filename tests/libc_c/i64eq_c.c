/* i64eq_c.c - int64.c with I64_ASM 0 and every function renamed c_..., for
 * t_i64eq.c to compare with the other build. */

#define I64_ASM 0
#define i64_set c_i64_set
#define i64_from_long c_i64_from_long
#define i64_is_zero c_i64_is_zero
#define i64_is_neg c_i64_is_neg
#define i64_add c_i64_add
#define i64_sub c_i64_sub
#define i64_mul c_i64_mul
#define i64_divu c_i64_divu
#define i64_divs c_i64_divs
#define i64_and c_i64_and
#define i64_or c_i64_or
#define i64_xor c_i64_xor
#define i64_neg c_i64_neg
#define i64_cpl c_i64_cpl
#define i64_shl c_i64_shl
#define i64_shr c_i64_shr
#define i64_cmp c_i64_cmp
#define i64_divsmall c_i64_divsmall
#define i64_muladd c_i64_muladd
#define i64_parse c_i64_parse
#define i64_str c_i64_str
#include "../../src/common/int64.c"
