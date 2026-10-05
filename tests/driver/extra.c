/* extra.c - compiled with agonc -c into libextra.s, a user library. */

int extra_add(int a, int b)
{
    return a + b;
}

/* Reached by nothing: linked only when named by -Wl,--entry=_extra_unused. */
int extra_unused(void)
{
    return 5;
}
