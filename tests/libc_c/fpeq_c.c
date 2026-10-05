/* fpeq_c.c - softfp.c with FP_ASM 0 and every function renamed c_..., for
 * t_fpeq.c to compare with the other build. */

#define FP_ASM 0
#define sf32_add c_sf32_add
#define sf32_cmp c_sf32_cmp
#define sf32_div c_sf32_div
#define sf32_from_decimal c_sf32_from_decimal
#define sf32_from_f64 c_sf32_from_f64
#define sf32_from_long c_sf32_from_long
#define sf32_from_long64 c_sf32_from_long64
#define sf32_mul c_sf32_mul
#define sf32_neg c_sf32_neg
#define sf32_sqrt c_sf32_sqrt
#define sf32_sub c_sf32_sub
#define sf32_to_long c_sf32_to_long
#define sf32_to_long64 c_sf32_to_long64
#define sf64_add c_sf64_add
#define sf64_cmp c_sf64_cmp
#define sf64_div c_sf64_div
#define sf64_from_decimal c_sf64_from_decimal
#define sf64_from_f32 c_sf64_from_f32
#define sf64_from_long c_sf64_from_long
#define sf64_from_long64 c_sf64_from_long64
#define sf64_mul c_sf64_mul
#define sf64_neg c_sf64_neg
#define sf64_sqrt c_sf64_sqrt
#define sf64_sub c_sf64_sub
#define sf64_to_decimal c_sf64_to_decimal
#define sf64_to_long c_sf64_to_long
#define sf64_to_long64 c_sf64_to_long64
#include "../../src/cc1/softfp.c"
