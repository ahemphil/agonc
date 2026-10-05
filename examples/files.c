/* files.c - writing, reading, appending, renaming and removing a file,
 * with nothing but the standard C library.
 *
 *     agonc -o files.bin files.c
 *     files
 *
 * The files are made in the current folder of the SD card and removed at
 * the end. Every call that can fail is checked: on the Agon a full card,
 * a missing folder or all eight of MOS's file handles in use are all
 * reported through the return value and errno, and perror turns errno
 * into a message.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NAME "notes.txt"
#define RENAMED "notes.old"

/* Stop with a message naming what failed. exit closes every open file,
 * which matters on the Agon: MOS does not close a program's files for
 * it when the program ends. */
static void fail(const char *what)
{
    perror(what);
    exit(EXIT_FAILURE);
}

/* Print the file's lines, numbered, and return how many there were. */
static int show(const char *name)
{
    FILE *f;
    char line[80];
    int n;

    f = fopen(name, "r");
    if (f == NULL)
        fail(name);
    n = 0;
    while (fgets(line, sizeof line, f) != NULL) {
        n++;
        printf("%2d: %s", n, line);     /* the line keeps its '\n' */
    }
    if (ferror(f))
        fail(name);
    fclose(f);
    return n;
}

int main(void)
{
    FILE *f;
    int i;
    long size;

    /* "w" makes the file, or empties one already there. In a text file
     * each '\n' is written as CR LF, as MOS's own tools expect, and read
     * back as '\n'. */
    f = fopen(NAME, "w");
    if (f == NULL)
        fail(NAME);
    fprintf(f, "Squares from agonc\n");
    for (i = 1; i <= 5; i++)
        fprintf(f, "%d squared is %d\n", i, i * i);
    if (fclose(f) != 0)                 /* a write error can show up only here */
        fail(NAME);

    printf("Written:\n");
    show(NAME);

    /* "a" adds to the end. */
    f = fopen(NAME, "a");
    if (f == NULL)
        fail(NAME);
    fputs("That is all.\n", f);
    fclose(f);

    /* A binary stream reads the bytes as they are, CR LF included, so
     * seeking to the end gives the size on the card. */
    f = fopen(NAME, "rb");
    if (f == NULL)
        fail(NAME);
    fseek(f, 0L, SEEK_END);
    size = ftell(f);
    fclose(f);

    printf("After appending:\n");
    printf("%d lines, %ld bytes on the card\n", show(NAME), size);

    /* rename refuses to replace a file that exists, so clear the way. */
    remove(RENAMED);
    if (rename(NAME, RENAMED) != 0)
        fail("rename");
    f = fopen(NAME, "r");               /* the old name should be gone */
    if (f != NULL) {
        fclose(f);
        printf("%s is still there\n", NAME);
        return EXIT_FAILURE;
    }
    printf("Renamed %s to %s\n", NAME, RENAMED);
    if (remove(RENAMED) != 0)
        fail("remove");
    printf("Removed %s\n", RENAMED);
    return 0;
}
