#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    puts("before");
    abort();
    return 1;
}
