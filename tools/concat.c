/* concat.c - join files into one: the C library's archive (libc.s) is its
 * units' .s files one after another, and MOS has no command for that.
 *
 *     concat out in...
 *
 * Returns 0, or 200 after a message if a file cannot be read or written
 * (the output is then removed). Built during the bootstrap by stage 1.
 */

#include <stdio.h>

char buf[1024];

int main(int argc, char **argv)
{
    FILE *out;
    FILE *in;
    int i;
    int n;

    if (argc < 3) {
        printf("usage: concat out in...\n");
        return 200;
    }
    out = fopen(argv[1], "wb");
    if (out == NULL) {
        printf("concat: cannot create %s\n", argv[1]);
        return 200;
    }
    for (i = 2; i < argc; i++) {
        in = fopen(argv[i], "rb");
        if (in == NULL) {
            printf("concat: cannot open %s\n", argv[i]);
            fclose(out);
            remove(argv[1]);
            return 200;
        }
        n = fread(buf, 1, 1024, in);
        while (n > 0) {
            fwrite(buf, 1, n, out);
            n = fread(buf, 1, 1024, in);
        }
        fclose(in);
    }
    if (fclose(out) != 0) {
        printf("concat: cannot write %s\n", argv[1]);
        remove(argv[1]);
        return 200;
    }
    return 0;
}
