/* t_intc.c - C test (test_libc_c.py): Ctrl-C at a console input prompt
 * takes effect when the line is entered (the harness types Ctrl-C and
 * then Enter): fgets returns, and SIGINT's default action ends the program
 * with 130 before the program sees the line. */

#include <stdio.h>

int main(void)
{
    char line[40];

    fclose(fopen("/out/m_c", "w"));
    fgets(line, sizeof line, stdin);
    return 1;
}
