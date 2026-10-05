/* t_util.c - t_drv.c's second unit. */

#include <drv.h>

static int which(void)
{
    return 2;
}

int util_which(void)
{
    return which();
}

int util_twice(int x)
{
    return x * 2;
}
