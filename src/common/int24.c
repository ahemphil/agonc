/* int24.c - see int24.h. */

#include "int24.h"

/* v modulo 2^24, read back as a signed 24-bit value: bits 0..23 are kept
 * and bit 23 is copied into every higher bit, so a 32-bit host int holds
 * the same number a 24-bit Agon int would. */
int wrap24(int v)
{
    unsigned u;

    u = v;
    u = u & 0xFFFFFFu;
    if (u & 0x800000u)
        u = u | ~0xFFFFFFu;     /* sign-extend above bit 23; no-op when int is 24-bit */
    return u;
}
