/* numbers.c - floating point and 64-bit integers, which the eZ80 does not
 * have in hardware: agonc's library does them in software, exactly as
 * IEEE 754 and C99 define them.
 *
 *     agonc -o numbers.bin numbers.c
 *     numbers
 *
 * The Mandelbrot set below takes about half a minute on an Agon: each of
 * its characters is up to 40 rounds of double-precision arithmetic. The
 * same program built on a PC prints exactly the same thing, digit for
 * digit.
 * long long needs agonc's default mode (it is C99's, not C89's).
 */

#include <stdio.h>
#include <math.h>

/* The Mandelbrot set as text: for each point c of the plane, iterate
 * z = z * z + c from z = 0 and see how soon |z| passes 2. Points that
 * never escape (within the limit) are in the set, printed as '#'; the
 * rest get a character for how quickly they escaped. */
static void mandelbrot(void)
{
    static const char shades[] = " .:-=+*%@";
    int row, col, n;
    double x, y, zr, zi, t;

    for (row = 0; row < 20; row++) {
        y = 1.14 - row * 0.12;          /* the rows straddle the real axis */
        for (col = 0; col < 64; col++) {
            x = -2.1 + col * 0.045;
            zr = 0.0;
            zi = 0.0;
            for (n = 0; n < 40 && zr * zr + zi * zi <= 4.0; n++) {
                t = zr * zr - zi * zi + x;
                zi = 2.0 * zr * zi + y;
                zr = t;
            }
            putchar(n == 40 ? '#' : shades[n % 9]);
        }
        putchar('\n');
    }
}

/* Some well-known results of IEEE 754 double precision. Every operation
 * is rounded to the nearest representable value, so 0.1 + 0.2 is not
 * quite 0.3, and %.17g prints enough digits to show it. sqrt is correctly
 * rounded, so squaring its result can miss 2 by one unit in the last
 * place. float has about 7 digits, double about 16. */
static void ieee(void)
{
    double r2;

    r2 = sqrt(2.0);
    printf("\n0.1 + 0.2 = %.17g\n", 0.1 + 0.2);
    printf("sqrt(2) = %.17g, and squared %.17g\n", r2, r2 * r2);
    printf("1/3 as a float %.9g, as a double %.17g\n", (double)(1.0f / 3.0f), 1.0 / 3.0);
}

/* Factorials, as far as an unsigned long long (64 bits) holds them:
 * 20! is the last. %llu prints one. */
static void factorials(void)
{
    unsigned long long f;
    int n;

    printf("\n");
    f = 1;
    for (n = 1; n <= 20; n++) {
        f = f * n;
        if (n % 4 == 0 || n == 20)
            printf("%2d! = %llu\n", n, f);
    }
}

int main(void)
{
    mandelbrot();
    ieee();
    factorials();
    printf("\nsizeof: int %d, long %d, long long %d, double %d\n",
           (int)sizeof(int), (int)sizeof(long), (int)sizeof(long long), (int)sizeof(double));
    return 0;
}
