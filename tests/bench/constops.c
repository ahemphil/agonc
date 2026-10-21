/* constops: / and % by 2, nibble masks and shifts by constants on int, as
 * in packing two decimal digits a byte, the way big-number programs
 * store their digits. */
#include "bench.h"

#define N 3000

static unsigned char nib[N];

int main(void)
{
    int i;
    int r;
    int x;
    unsigned long sum;

    bench_start();
    sum = 0;
    for (r = 0; r < 4; r++) {
        for (i = 0; i < 2 * N; i++) {   /* pack: two digits a byte */
            x = (i * 7 + r) & 7;
            if (i % 2)
                nib[i / 2] = (unsigned char)((nib[i / 2] & 0x0F) | (x << 4));
            else
                nib[i / 2] = (unsigned char)((nib[i / 2] & 0xF0) | x);
        }
        for (i = 0; i < 2 * N; i++) {   /* unpack */
            x = i % 2 ? nib[i / 2] >> 4 : nib[i / 2] & 0x0F;
            sum += (unsigned long)x;
            sum ^= (unsigned long)((i >> 3) & 0xFF);
        }
    }
    return bench_end("constops", sum);
}
