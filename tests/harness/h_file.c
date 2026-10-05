#include <stdio.h>

int main(void)
{
    char line[40];
    FILE *f;

    f = fopen("in.txt", "r");
    if (f == NULL)
        return 1;
    fgets(line, 40, f);
    fclose(f);
    printf("%s", line);
    return 0;
}
