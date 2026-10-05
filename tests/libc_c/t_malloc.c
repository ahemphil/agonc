/* t_malloc.c - malloc.c on the emulator, porting the cases of the assembly
 * malloc.s's four tests (tests/libc) and adding its growth paths
 * and the stack margin. Each test frees what it allocates, so each starts
 * from an empty heap (_heap_top back at __image_end).
 */

#include <stdlib.h>
#include <string.h>
#include "check.h"

extern char *_heap_top;         /* malloc.c's __heap_top */
extern char _image_end[];       /* ld's __image_end */

#define HDR 4

void test_basic(void)
{
    char *p;
    char *q;
    char *r;
    int i;

    check(_heap_top == _image_end, 1);
    p = malloc(10);
    check(p == _image_end + HDR, 1);    /* the first block */
    for (i = 0; i < 10; i++)
        p[i] = i;
    q = malloc(20);
    check(q != NULL, 1);
    check(q >= p + 10, 1);              /* no overlap */
    memset(q, 0x55, 20);
    check(p[9], 9);                     /* p survived q's writes */
    free(q);
    r = malloc(20);
    check(r == q, 1);                   /* the same size reuses the address */
    free(r);
    q = malloc(0);
    check(q != NULL, 1);
    free(q);
    free(p);
    free(NULL);
    check(_heap_top == _image_end, 1);
}

void test_coalesce(void)
{
    char *a;
    char *b;
    char *c;
    char *d;

    a = malloc(10);
    b = malloc(10);
    c = malloc(10);
    d = malloc(10);
    free(b);
    free(a);                            /* merges forward into b */
    check(malloc(24) == a, 1);          /* 10 + header + 10 */
    free(a);
    free(c);                            /* a..c free: merges backward */
    check(malloc(38) == a, 1);          /* 10 + 4 + 10 + 4 + 10 */
    free(a);
    free(d);                            /* everything free: all given back */
    check(_heap_top == _image_end, 1);
    a = malloc(10);
    b = malloc(10);
    c = malloc(10);
    free(b);
    free(c);                            /* the last block: the top comes down to b */
    check(_heap_top == b - HDR, 1);
    free(a);
    check(_heap_top == _image_end, 1);
}

void test_limits(void)
{
    char *top;
    char *p;
    char here;
    int avail;

    top = _heap_top;
    check(malloc(1000000) == NULL, 1);  /* more than the machine has */
    check(_heap_top == top, 1);
    check(malloc(0xFFFFFF) == NULL, 1);
    check(calloc(0x100000, 0x100) == NULL, 1);  /* the product overflows */
    check(_heap_top == top, 1);
    /* the heap stays 256 bytes clear of the stack pointer */
    avail = &here - _heap_top;
    check(malloc(avail - HDR - 100) == NULL, 1);
    p = malloc(avail - HDR - 400);
    check(p != NULL, 1);
    free(p);
    p = malloc(10);
    check(p == _image_end + HDR, 1);    /* still working normally */
    free(p);
    check(_heap_top == _image_end, 1);
}

void test_calloc_realloc(void)
{
    char *p;
    char *q;
    char *r;
    char *g;
    int i;
    int zero;

    p = malloc(32);
    g = malloc(4);                      /* keeps p from being the last block */
    memset(p, 0xAA, 32);
    free(p);
    q = calloc(8, 4);
    check(q == p, 1);                   /* the same memory, now zeroed */
    zero = 1;
    for (i = 0; i < 32; i++)
        if (q[i] != 0)
            zero = 0;
    check(zero, 1);
    free(q);
    q = realloc(NULL, 10);              /* realloc(NULL, n) is malloc(n) */
    check(q == p, 1);
    check(realloc(q, 0) == NULL, 1);    /* realloc(p, 0) frees */
    check(malloc(10) == p, 1);
    /* shrink: same pointer, bytes kept, the tail reusable */
    memcpy(p, "abcdefghij", 10);
    q = realloc(p, 3);
    check(q == p, 1);
    check_mem(q, "abc", 3);
    r = malloc(3);
    check(r == p + 3 + HDR, 1);
    free(r);
    free(q);
    free(g);
    check(_heap_top == _image_end, 1);
    /* grow the last block in place */
    p = malloc(8);
    memcpy(p, "12345678", 8);
    q = realloc(p, 100);
    check(q == p, 1);
    check(_heap_top == p + 100, 1);
    check_mem(q, "12345678", 8);
    free(q);
    /* grow into a following free block */
    p = malloc(10);
    q = malloc(20);
    r = malloc(4);
    memcpy(p, "0123456789", 10);
    free(q);
    q = realloc(p, 25);                 /* 10 + 4 + 20 = 34 available */
    check(q == p, 1);
    check_mem(q, "0123456789", 10);
    g = malloc(5);                      /* the 9 left over became a block */
    check(g == p + 25 + HDR, 1);
    free(g);
    free(q);
    free(r);
    check(_heap_top == _image_end, 1);
    /* grow by moving: the block after is in use */
    p = malloc(10);
    q = malloc(10);
    memcpy(p, "moving!!!!", 10);
    r = realloc(p, 50);
    check(r != p, 1);
    check_mem(r, "moving!!!!", 10);
    check(malloc(10) == p, 1);          /* the old block was freed */
    free(p);
    free(q);
    free(r);
    check(_heap_top == _image_end, 1);
    /* a failed realloc leaves the block alone */
    p = malloc(10);
    memcpy(p, "keep", 5);
    check(realloc(p, 0x7FFFFF) == NULL, 1);
    check_mem(p, "keep", 5);
    free(p);
}

int main(void)
{
    test_basic();
    test_coalesce();
    test_limits();
    test_calloc_realloc();
    return finish();
}
