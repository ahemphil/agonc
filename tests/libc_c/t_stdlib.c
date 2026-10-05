/* t_stdlib.c - M12 step 5: the rest of <stdlib.h> (qsort, bsearch, rand,
 * div, getenv, system, the multibyte functions), of <string.h> and
 * <ctype.h>, and <locale.h>. getenv is NULL here (MOS 2 has no variables);
 * on MOS 3, test_libc_c.py's G test sets one first. */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <locale.h>
#include <limits.h>
#include "check.h"

int ints[20];
char *words[6];

struct pair {
    int key;
    char tag;
};

struct pair pairs[5];

int by_int(const void *a, const void *b)
{
    int x;
    int y;

    x = *(const int *)a;
    y = *(const int *)b;
    return x < y ? -1 : x > y;
}

int by_str(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

int by_key(const void *a, const void *b)
{
    return ((const struct pair *)a)->key - ((const struct pair *)b)->key;
}

int sorted(int n)
{
    int i;

    for (i = 1; i < n; i++)
        if (ints[i - 1] > ints[i])
            return 0;
    return 1;
}

void test_sort(void)
{
    int i;
    int key;
    int *hit;

    for (i = 0; i < 20; i++)
        ints[i] = (i * 7919) % 23 - 11;         /* a scramble with negatives */
    qsort(ints, 20, sizeof(int), by_int);
    check(sorted(20), 1);
    for (i = 0; i < 20; i++)
        ints[i] = 20 - i;                       /* reversed */
    qsort(ints, 20, sizeof(int), by_int);
    check(sorted(20), 1);
    check(ints[0], 1);
    check(ints[19], 20);
    for (i = 0; i < 20; i++)
        ints[i] = i % 3;                        /* duplicates */
    qsort(ints, 20, sizeof(int), by_int);
    check(sorted(20), 1);
    ints[0] = 5;
    qsort(ints, 1, sizeof(int), by_int);        /* one, and none: untouched */
    qsort(ints, 0, sizeof(int), by_int);
    check(ints[0], 5);
    words[0] = "pear";
    words[1] = "apple";
    words[2] = "fig";
    words[3] = "date";
    words[4] = "cherry";
    words[5] = "banana";
    qsort(words, 6, sizeof(char *), by_str);
    check_str(words[0], "apple");
    check_str(words[5], "pear");
    pairs[0].key = 3;
    pairs[0].tag = 'c';
    pairs[1].key = 1;
    pairs[1].tag = 'a';
    pairs[2].key = 5;
    pairs[2].tag = 'e';
    pairs[3].key = 2;
    pairs[3].tag = 'b';
    pairs[4].key = 4;
    pairs[4].tag = 'd';
    qsort(pairs, 5, sizeof(struct pair), by_key);   /* 4-byte elements */
    check(pairs[0].tag, 'a');
    check(pairs[4].tag, 'e');
    check(pairs[2].key, 3);
    /* bsearch in 0, 3, 6, ... 57 */
    for (i = 0; i < 20; i++)
        ints[i] = 3 * i;
    key = 42;
    hit = bsearch(&key, ints, 20, sizeof(int), by_int);
    check(hit == ints + 14, 1);
    key = 0;
    check(bsearch(&key, ints, 20, sizeof(int), by_int) == ints, 1);
    key = 57;
    check(bsearch(&key, ints, 20, sizeof(int), by_int) == ints + 19, 1);
    key = 43;
    check(bsearch(&key, ints, 20, sizeof(int), by_int) == NULL, 1);
    check(bsearch(&key, ints, 0, sizeof(int), by_int) == NULL, 1);
}

void test_misc(void)
{
    div_t d;
    ldiv_t ld;
    wchar_t wc;
    wchar_t wide[4];
    char narrow[4];
    int i;

    /* C89 4.10.2.2's generator, from seed 1 */
    check(rand(), 16838);
    check(rand(), 5758);
    check(rand(), 10113);
    srand(1);
    check(rand(), 16838);
    check(RAND_MAX, 32767);
    /* div and ldiv truncate toward zero */
    d = div(7, 2);
    check(d.quot, 3);
    check(d.rem, 1);
    d = div(-7, 2);
    check(d.quot, -3);
    check(d.rem, -1);
    ld = ldiv(-100000L, 7L);
    check(ld.quot == -14285L, 1);
    check(ld.rem == -5L, 1);
    /* the environment */
    check(getenv("NoSuchVariable") == NULL, 1);
    check(system(NULL) != 0, 1);
    /* multibyte: one byte each */
    check(MB_CUR_MAX, 1);
    check(mblen(NULL, 0), 0);
    check(mblen("x", 1), 1);
    check(mblen("", 1), 0);
    check(mbtowc(&wc, "\xe9", 1), 1);
    check(wc, 0xe9);
    check(wctomb(narrow, 'q'), 1);
    check(narrow[0], 'q');
    check(wctomb(narrow, 256), -1);
    check((int)mbstowcs(wide, "ab", 4), 2);
    check(wide[1], 'b');
    check(wide[2], 0);
    check((int)wcstombs(narrow, wide, 4), 2);
    check_str(narrow, "ab");
    wide[0] = 300;
    check(wcstombs(narrow, wide, 4) == (size_t)-1, 1);
    /* ctype: the classes over every byte */
    for (i = -1, d.quot = 0, d.rem = 0; i < 256; i++) {
        d.quot = d.quot + (ispunct(i) != 0);
        d.rem = d.rem + (iscntrl(i) != 0);
    }
    check(d.quot, 32);
    check(d.rem, 33);
    check(isgraph(' '), 0);
    check(isgraph('~') != 0, 1);
    check(ispunct('a'), 0);
    check(ispunct('@') != 0, 1);
    check(iscntrl(127) != 0, 1);
    check(iscntrl(-1), 0);                /* EOF */
}

void test_string(void)
{
    char s[32];
    char *t;

    check(memchr("abcdef", 'd', 6) != NULL, 1);
    check((char *)memchr("abcdef", 'd', 3) == NULL, 1);
    check(*(char *)memchr("ab\0cd", 'c', 5), 'c');      /* past a NUL */
    strcpy(s, "ab");
    check_str(strncat(s, "cdef", 2), "abcd");
    check(strcoll("a", "b") < 0, 1);
    check((int)strxfrm(s, "xyz", 10), 3);
    check_str(s, "xyz");
    check((int)strspn("aabxc", "ab"), 3);
    check((int)strcspn("xyzab", "ab"), 3);
    check((int)strcspn("xyz", "ab"), 3);
    check_str(strpbrk("hello, world", " ,"), ", world");
    check(strpbrk("hello", "xyz") == NULL, 1);
    check_str(strstr("haystack needle", "needle"), "needle");
    check_str(strstr("abc", ""), "abc");
    check(strstr("abc", "abcd") == NULL, 1);
    check_str(strstr("aab", "ab"), "ab");
    strcpy(s, "  one,two ,, three  ");
    t = strtok(s, " ,");
    check_str(t, "one");
    t = strtok(NULL, " ,");
    check_str(t, "two");
    t = strtok(NULL, " ,");
    check_str(t, "three");
    check(strtok(NULL, " ,") == NULL, 1);
    check(strtok(NULL, " ,") == NULL, 1);
}

void test_locale(void)
{
    struct lconv *lc;

    check_str(setlocale(LC_ALL, NULL), "C");
    check_str(setlocale(LC_ALL, "C"), "C");
    check_str(setlocale(LC_NUMERIC, ""), "C");
    check(setlocale(LC_ALL, "fr_FR") == NULL, 1);
    check(setlocale(99, "C") == NULL, 1);
    lc = localeconv();
    check_str(lc->decimal_point, ".");
    check_str(lc->thousands_sep, "");
    check(lc->int_frac_digits, CHAR_MAX);
    check(lc->n_sign_posn, CHAR_MAX);
}

int main(void)
{
    test_sort();
    test_misc();
    test_string();
    test_locale();
    return finish();
}
