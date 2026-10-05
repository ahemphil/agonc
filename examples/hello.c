/* hello.c - the smallest complete program.
 *
 *     agonc -o hello.bin hello.c
 *     hello
 *
 * main's return value is the program's status: 0 tells MOS (and a MOS
 * exec script) that all went well.
 */

#include <stdio.h>

int main(void)
{
    printf("Hello from agonc\n");
    return 0;
}
