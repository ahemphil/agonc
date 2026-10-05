/* fpeq_a.c - softfp.c with FP_ASM 1 and every function renamed a_..., for
 * t_fpeq.c to compare with the other build. */

#define FP_ASM 1
#define sf32_add a_sf32_add
#define sf32_cmp a_sf32_cmp
#define sf32_div a_sf32_div
#define sf32_from_decimal a_sf32_from_decimal
#define sf32_from_f64 a_sf32_from_f64
#define sf32_from_long a_sf32_from_long
#define sf32_from_long64 a_sf32_from_long64
#define sf32_mul a_sf32_mul
#define sf32_neg a_sf32_neg
#define sf32_sqrt a_sf32_sqrt
#define sf32_sub a_sf32_sub
#define sf32_to_long a_sf32_to_long
#define sf32_to_long64 a_sf32_to_long64
#define sf64_add a_sf64_add
#define sf64_cmp a_sf64_cmp
#define sf64_div a_sf64_div
#define sf64_from_decimal a_sf64_from_decimal
#define sf64_from_f32 a_sf64_from_f32
#define sf64_from_long a_sf64_from_long
#define sf64_from_long64 a_sf64_from_long64
#define sf64_mul a_sf64_mul
#define sf64_neg a_sf64_neg
#define sf64_sqrt a_sf64_sqrt
#define sf64_sub a_sf64_sub
#define sf64_to_decimal a_sf64_to_decimal
#define sf64_to_long a_sf64_to_long
#define sf64_to_long64 a_sf64_to_long64
#include "../../src/cc1/softfp.c"
