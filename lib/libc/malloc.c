/* malloc.c - the heap of abi.md section 8: malloc, calloc, realloc, free.
 *
 * A first-fit allocator growing up from __image_end. The heap and the
 * stack share everything between the image and bss with no fixed split;
 * the heap refuses to grow within 256 bytes of the stack pointer and
 * returns a null pointer instead.
 *
 * The heap is an implicit list: every block is a 4-byte header (its
 * payload size and a used flag) followed by its payload, and the next
 * block starts right after, up to _heap_top. Two invariants make free()
 * complete: no two free blocks are ever adjacent (free merges forwards and
 * backwards), and the last block is never free (it is given back by
 * lowering _heap_top). A search or a free is linear in the number of
 * blocks, which is small on a 512 KB machine; in exchange there are no
 * free-list pointers to go stale.
 *
 *   __image_end                                         _heap_top
 *   | size used | payload ... | size free | ... | size used | ... |
 *   '-- HDR ---'
 *
 * malloc returns the address just past a header, so free and realloc find
 * the header HDR bytes below the pointer they are given. Nothing aligns a
 * block: the eZ80 reads any object at any address.
 *
 * tests/libc_c/t_malloc.c defines the behaviour.
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>

struct block {
    unsigned int size;          /* payload bytes */
    char used;
};

#define HDR 4                   /* sizeof(struct block): no padding (abi.md section 2) */
#define MARGIN 256              /* kept free below the stack pointer */
#define MAX_REQUEST 0x700000    /* nothing larger can fit in 512 KB; avoids 24-bit wrap */

extern char _image_end[];               /* __image_end, placed by ld */
char *_heap_top = _image_end;           /* __heap_top: one past the last block */

#ifdef __AGONC_TEST
char *__test_heap_max;                  /* the highest _heap_top (memprofile.py --device) */
#endif

/* The block that follows b in memory (or _heap_top, if b is the last). */
static struct block *after(struct block *b)
{
    return (struct block *)((char *)b + HDR + b->size);
}

/* Room to extend the heap by n bytes without coming within MARGIN of the
 * stack (a local's address stands for the stack pointer). MARGIN leaves
 * room for the stack to grow in the calls that follow; a stack that grows
 * past it into the heap is not caught (abi.md 8). */
static int room_for(unsigned int n)
{
    char probe;

    if (_heap_top > &probe)
        return 0;
    return (unsigned int)(&probe - _heap_top) >= n + MARGIN;
}

/* Split used block b to size bytes if the rest can hold a header and at
 * least one byte; the rest becomes a free block (the caller merges it). */
static struct block *split(struct block *b, unsigned int size)
{
    struct block *rest;

    if (b->size < size + HDR + 1)
        return NULL;
    rest = (struct block *)((char *)b + HDR + size);
    rest->size = b->size - size - HDR;
    rest->used = 0;
    b->size = size;
    return rest;
}

/* The block before b, or NULL if b is the first: a walk from the heap's
 * start, since a header records only its own block's size. (A footer
 * repeating the size at each block's end, a "boundary tag", would find
 * the previous block at once, at the cost of more bytes per block.) */
static struct block *before(struct block *b)
{
    struct block *p;
    struct block *prev;

    prev = NULL;
    p = (struct block *)_image_end;
    while (p != b) {
        prev = p;
        p = after(p);
    }
    return prev;
}

/* Restore the invariants around free block b: merge with a free
 * neighbour on either side, and give it back if it ends the heap. */
static void settle(struct block *b)
{
    struct block *n;
    struct block *p;

    /* merge forwards: b absorbs the next block's header and payload */
    n = after(b);
    if ((char *)n < _heap_top && !n->used)
        b->size = b->size + HDR + n->size;
    /* merge backwards: the previous block absorbs b */
    p = before(b);
    if (p != NULL && !p->used) {
        p->size = p->size + HDR + b->size;
        b = p;
    }
    /* a free last block is given back, keeping the last block used */
    if ((char *)after(b) == _heap_top)
        _heap_top = (char *)b;
}

/* NULL, with errno ENOMEM, if there is no room. First fit: the first free
 * block big enough is used, its surplus split off; failing that, a block
 * is added at _heap_top. malloc(0) gets a one-byte block, so it returns a
 * unique pointer. */
void *malloc(unsigned int size)
{
    struct block *b;
    struct block *rest;

    if (size > MAX_REQUEST) {
        errno = ENOMEM;
        return NULL;
    }
    if (size == 0)
        size = 1;
    for (b = (struct block *)_image_end; (char *)b < _heap_top; b = after(b)) {
        if (!b->used && b->size >= size) {
            b->used = 1;
            rest = split(b, size);
            if (rest != NULL)
                settle(rest);
            return (char *)b + HDR;
        }
    }
    if (!room_for(HDR + size)) {
        errno = ENOMEM;
        return NULL;
    }
    b = (struct block *)_heap_top;
    b->size = size;
    b->used = 1;
    _heap_top = _heap_top + HDR + size;
#ifdef __AGONC_TEST
    if (_heap_top > __test_heap_max)
        __test_heap_max = _heap_top;
#endif
    return (char *)b + HDR;
}

/* No check is made that ptr came from malloc or is not already free. */
void free(void *ptr)
{
    struct block *b;

    if (ptr == NULL)
        return;
    b = (struct block *)((char *)ptr - HDR);
    b->used = 0;
    settle(b);
}

/* A zero count or size is taken as one byte, as malloc(0) is. The
 * division tests nmemb * size against MAX_REQUEST before multiplying, so
 * a product that would wrap in 24 bits is refused instead of allocating a
 * small block. */
void *calloc(unsigned int nmemb, unsigned int size)
{
    unsigned int total;
    void *p;

    if (nmemb == 0 || size == 0) {
        nmemb = 1;
        size = 1;
    }
    if (nmemb > MAX_REQUEST / size) {
        errno = ENOMEM;
        return NULL;
    }
    total = nmemb * size;
    p = malloc(total);
    if (p != NULL)
        memset(p, 0, total);
    return p;
}

/* In place where it can be: a shrink, growth of the last block into the
 * free space above it, or growth into a free block that follows; else a
 * new block, the old contents copied and the old block freed. NULL (the
 * old block untouched) if there is no room. */
void *realloc(void *ptr, unsigned int newsize)
{
    struct block *b;
    struct block *n;
    struct block *rest;
    void *q;

    if (ptr == NULL)
        return malloc(newsize);
    if (newsize == 0) {
        free(ptr);
        return NULL;
    }
    if (newsize > MAX_REQUEST) {
        errno = ENOMEM;
        return NULL;
    }
    b = (struct block *)((char *)ptr - HDR);
    if (newsize <= b->size) {
        /* shrink in place; a tail big enough for a block is given back */
        rest = split(b, newsize);
        if (rest != NULL)
            settle(rest);
        return ptr;
    }
    n = after(b);
    if ((char *)n == _heap_top) {
        /* the last block: extend the heap under it */
        if (room_for(newsize - b->size)) {
            _heap_top = _heap_top + (newsize - b->size);
#ifdef __AGONC_TEST
            if (_heap_top > __test_heap_max)
                __test_heap_max = _heap_top;
#endif
            b->size = newsize;
            return ptr;
        }
    } else if (!n->used && b->size + HDR + n->size >= newsize) {
        /* absorb the free block after it, then give back what is left */
        b->size = b->size + HDR + n->size;
        rest = split(b, newsize);
        if (rest != NULL)
            settle(rest);
        return ptr;
    }
    q = malloc(newsize);
    if (q == NULL)
        return NULL;
    memcpy(q, ptr, b->size);
    free(ptr);
    return q;
}
