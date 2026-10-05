/* list: pointers and structs - a sorted linked list, struct copies, and
 * structs passed and returned by value. */
#include "bench.h"

#define N 400

struct node {
    int key;
    long val;
    struct node *next;
};

struct pair {
    long a;
    long b;
};

static struct node pool[N];

static struct pair add(struct pair p, struct pair q)
{
    struct pair r;

    r.a = p.a + q.a;
    r.b = p.b ^ q.b;
    return r;
}

int main(void)
{
    struct node *head;
    struct node **pp;
    struct node *n;
    struct pair acc;
    struct pair one;
    int i;
    int r;
    unsigned long sum;

    bench_start();
    sum = 0;
    for (r = 0; r < 5; r++) {
        head = 0;
        for (i = 0; i < N; i++) {
            n = &pool[i];
            n->key = (i * 37 + r) % N;
            n->val = (long)i * 1000L;
            for (pp = &head; *pp != 0 && (*pp)->key < n->key; pp = &(*pp)->next)
                ;
            n->next = *pp;
            *pp = n;
        }
        acc.a = 0;
        acc.b = 0;
        for (n = head; n != 0; n = n->next) {
            one.a = n->val;
            one.b = (long)n->key;
            acc = add(acc, one);
        }
        sum = (sum + (unsigned long)acc.a + (unsigned long)acc.b + (unsigned long)head->key) & 0xFFFFFFFFUL;
    }
    return bench_end("list", sum);
}
