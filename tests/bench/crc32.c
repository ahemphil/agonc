/* crc32: bitwise CRC-32, 32-bit shifts, masks and exclusive or. */
#include "bench.h"

#define LEN 1024

static unsigned char buf[LEN];

static unsigned long crc32(const unsigned char *p, int n)
{
    unsigned long crc;
    int k;

    crc = 0xFFFFFFFFUL;
    while (n-- > 0) {
        crc ^= *p++;
        for (k = 0; k < 8; k++)
            crc = crc & 1 ? crc >> 1 ^ 0xEDB88320UL : crc >> 1;
    }
    return ~crc & 0xFFFFFFFFUL;
}

int main(void)
{
    int i;
    int r;
    unsigned long sum;

    bench_start();
    for (i = 0; i < LEN; i++)
        buf[i] = (unsigned char)(i * 7 + 3);
    sum = 0;
    for (r = 0; r < 8; r++) {
        buf[r] ^= 0x55;
        sum ^= crc32(buf, LEN);
    }
    return bench_end("crc32", sum);
}
