/* testmain.c - the entry point of a test build (tests/tools/testrun.py).
 *
 * crt0_test.s is crt0.s calling __test_main instead of main. The first
 * argument names the file the program's console output goes to (stdio.c's
 * test build appends to it); it is removed before main sees the rest.
 * main's return goes to exit, whose test build records the status in
 * <name>.st and returns 0 to MOS, so a failing test does not stop the batch
 * script it runs in. The
 * output file is emptied first: a crash can reset the machine, and MOS then
 * runs the batch again from the start. */

#include <agon/mos.h>
#include <string.h>

extern char *__test_out;
int main(int argc, char **argv);

#ifdef __AGONC_MEMPROFILE
/* memprofile.py --device builds its programs with this variant, which
 * records the heap's and the stack's high-water marks in <output>.mem:
 * the heap's is malloc's (__test_heap_max); for the stack, the space
 * between the image and the stack is filled with a pattern before main,
 * and at exit the deepest byte of the stack is the first one above the
 * heap's high-water mark no longer holding it. */
#define FILL 0xA5

extern char _image_end[];
extern char _stack_top[];
extern char *__test_heap_max;
extern void (*__test_at_status)(void);

static char *num(char *p, unsigned n)
{
    char digits[10];
    int i;

    i = 0;
    do {
        digits[i++] = (char)('0' + n % 10);
        n = n / 10;
    } while (n > 0);
    while (i > 0)
        *p++ = digits[--i];
    return p;
}

static void record(void)
{
    char name[80];
    char text[48];
    char *heap;
    unsigned char *p;
    char *t;
    int h;

    heap = __test_heap_max != NULL ? __test_heap_max : _image_end;
    p = (unsigned char *)heap;
    while ((char *)p < _stack_top && *p == FILL)
        p++;
    t = text;
    memcpy(t, "heap ", 5);
    t = num(t + 5, (unsigned)(heap - _image_end));
    memcpy(t, " stack ", 7);
    t = num(t + 7, (unsigned)(_stack_top - (char *)p));
    *t++ = '\n';
    strcpy(name, __test_out);
    strcat(name, ".mem");
    h = mos_fopen(name, FA_WRITE | FA_CREATE_ALWAYS);
    if (h != 0) {
        mos_fwrite(h, text, (int)(t - text));
        mos_fclose(h);
    }
}

static void fill(void)
{
    char here;

    memset(_image_end, FILL, (unsigned)(&here - 256 - _image_end));
    __test_at_status = record;
}
#endif

#ifdef __AGONC_TIMING
/* The benchmarks (tests/bench) build their programs with this
 * variant, which records how long main ran, in clock()'s hundredths of a
 * second (MOS's timer), in <output>.tm. */
#include <time.h>

extern void (*__test_at_status)(void);
static clock_t t0;

static void record_time(void)
{
    char name[80];
    char digits[12];
    char text[12];
    unsigned long n;
    int i;
    int k;
    int h;

    n = (unsigned long)(clock() - t0);
    i = 0;
    do {
        digits[i++] = (char)('0' + (int)(n % 10));
        n = n / 10;
    } while (n > 0);
    k = 0;
    while (i > 0)
        text[k++] = digits[--i];
    text[k++] = '\n';
    strcpy(name, __test_out);
    strcat(name, ".tm");
    h = mos_fopen(name, FA_WRITE | FA_CREATE_ALWAYS);
    if (h != 0) {
        mos_fwrite(h, text, k);
        mos_fclose(h);
    }
}
#endif

int __test_main(int argc, char **argv)
{
    int h;

#ifdef __AGONC_MEMPROFILE
    fill();
#endif

    if (argc >= 2) {
        __test_out = argv[1];
        h = mos_fopen(__test_out, FA_WRITE | FA_CREATE_ALWAYS);
        if (h != 0)
            mos_fclose(h);          /* (mos_fclose(0) would close every file) */
        argv[1] = argv[0];
        argc--;
        argv++;
    }
#ifdef __AGONC_TIMING
    __test_at_status = record_time;
    t0 = clock();
#endif
    return main(argc, argv);
}
