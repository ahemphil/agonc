/* bench.h - the benchmarks' harness (tests/bench/bench.py).
 *
 * A benchmark times its work with clock() (MOS's timer, hundredths of a
 * second) and ends with bench_end(name, checksum). bench.py first builds
 * it on the PC with -DBENCH_HOST, where bench_end prints the checksum;
 * the Agon build gets that as -DWANT=..., writes "<ticks> <checksum>" to
 * /b_<name>.txt on the card, and exits 0 only if the checksums agree, so
 * a wrong answer stops the emulator's script instead of counting as a
 * time. Nothing goes to the screen (agon-emulator-output-flakiness).
 * Checksums are kept to 32 bits and nothing depends on int's width, so
 * the PC's answer is the Agon's.
 */

#include <stdio.h>
#include <time.h>

#ifndef WANT
#define WANT 0UL
#endif

static clock_t bench_t0;

static void bench_start(void)
{
    bench_t0 = clock();
}

static int bench_end(const char *name, unsigned long check)
{
#ifdef BENCH_HOST
    printf("%lu\n", check & 0xFFFFFFFFUL);
    return 0;
#else
    clock_t t;
    FILE *f;
    char path[40];

    t = clock() - bench_t0;
    sprintf(path, "/b_%s.txt", name);
    f = fopen(path, "w");
    if (f != NULL) {
        fprintf(f, "%ld %lu\n", (long)t, check);
        fclose(f);
    }
    return (check & 0xFFFFFFFFUL) == WANT ? 0 : 1;
#endif
}
