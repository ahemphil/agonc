/* compare.c - are two files identical? The bootstrap's last step uses it to
 * check that stage 3 equals stage 2.
 *
 *     compare a b
 *
 * Prints "same" or where the files first differ. Returns 0 if they are
 * identical, else 200, which stops a MOS script there. Built during the
 * bootstrap by stage 1.
 */

#include <stdio.h>

char buf_a[512];
char buf_b[512];

int main(int argc, char **argv)
{
    FILE *a;
    FILE *b;
    int na;
    int nb;
    int i;
    int pos;
    int result;

    if (argc != 3) {
        printf("usage: compare a b\n");
        return 200;
    }
    a = fopen(argv[1], "rb");
    b = fopen(argv[2], "rb");
    if (a == NULL || b == NULL) {
        printf("compare: cannot open %s\n", a == NULL ? argv[1] : argv[2]);
        if (a != NULL)
            fclose(a);
        if (b != NULL)
            fclose(b);
        return 200;
    }
    pos = 0;
    result = -1;
    while (result < 0) {
        na = fread(buf_a, 1, 512, a);
        nb = fread(buf_b, 1, 512, b);
        for (i = 0; i < na && i < nb && buf_a[i] == buf_b[i]; i++)
            ;
        if (i < na && i < nb) {
            printf("DIFFERENT: %s and %s differ at byte %d\n", argv[1], argv[2], pos + i);
            result = 200;
        } else if (na != nb) {
            printf("DIFFERENT: %s and %s differ in length\n", argv[1], argv[2]);
            result = 200;
        } else if (na == 0) {
            printf("same: %s %s (%d bytes)\n", argv[1], argv[2], pos);
            result = 0;
        }
        pos = pos + na;
    }
    fclose(a);
    fclose(b);
    return result;
}
