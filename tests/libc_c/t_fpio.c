/* t_fpio.c - the floating-point conversions of <stdio.h> and <stdlib.h>
 * (M13): printf's e f g E F G with their flags, widths and precisions
 * (the expected text is Python's, whose % formatting is C's, exactly
 * rounded), scanf's, strtod and atof (the expected bits are the PC's).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "check.h"

char buf[400];
char big[1000];

unsigned long word(unsigned char *p)
{
    return (unsigned long)p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}

void checkd(double d, unsigned long hi, unsigned long lo)
{
    unsigned char *p;

    p = (unsigned char *)&d;
    check(word(p + 4) == hi && word(p) == lo, 1);
}

void checkf(float f, unsigned long bits)
{
    check(word((unsigned char *)&f) == bits, 1);
}

/* printf's floating conversions, cases 1 to 20 */
void print1(void)
{
    check(sprintf(buf, "%e", 0.0), 12);
    check_str(buf, "0.000000e+00");
    check(sprintf(buf, "%f", 0.0), 8);
    check_str(buf, "0.000000");
    check(sprintf(buf, "%g", 0.0), 1);
    check_str(buf, "0");
    check(sprintf(buf, "%.0e", 0.0), 5);
    check_str(buf, "0e+00");
    check(sprintf(buf, "%#.0f", 0.0), 2);
    check_str(buf, "0.");
    check(sprintf(buf, "%#g", 0.0), 7);
    check_str(buf, "0.00000");
    check(sprintf(buf, "%+.2f", 0.0), 5);
    check_str(buf, "+0.00");
    check(sprintf(buf, "%f", -0.0), 9);
    check_str(buf, "-0.000000");
    check(sprintf(buf, "%g", -0.0), 2);
    check_str(buf, "-0");
    check(sprintf(buf, "%e", -0.0), 13);
    check_str(buf, "-0.000000e+00");
    check(sprintf(buf, "%e", 1.0), 12);
    check_str(buf, "1.000000e+00");
    check(sprintf(buf, "%f", 1.0), 8);
    check_str(buf, "1.000000");
    check(sprintf(buf, "%g", 1.0), 1);
    check_str(buf, "1");
    check(sprintf(buf, "%.0f", 1.0), 1);
    check_str(buf, "1");
    check(sprintf(buf, "%#.3g", 1.0), 4);
    check_str(buf, "1.00");
    check(sprintf(buf, "%010.3f", 1.0), 10);
    check_str(buf, "000001.000");
    check(sprintf(buf, "% f", 1.0), 9);
    check_str(buf, " 1.000000");
    check(sprintf(buf, "%f", -1.5), 9);
    check_str(buf, "-1.500000");
    check(sprintf(buf, "%.0f", -1.5), 2);
    check_str(buf, "-2");
    check(sprintf(buf, "%g", -1.5), 4);
    check_str(buf, "-1.5");
}

/* printf's floating conversions, cases 21 to 40 */
void print2(void)
{
    check(sprintf(buf, "%E", -1.5), 13);
    check_str(buf, "-1.500000E+00");
    check(sprintf(buf, "%010.2f", -1.5), 10);
    check_str(buf, "-000001.50");
    check(sprintf(buf, "%-10.2f|", -1.5), 11);
    check_str(buf, "-1.50     |");
    check(sprintf(buf, "%+g", -1.5), 4);
    check_str(buf, "-1.5");
    check(sprintf(buf, "%.17g", 0.1), 19);
    check_str(buf, "0.10000000000000001");
    check(sprintf(buf, "%.20e", 0.1), 26);
    check_str(buf, "1.00000000000000005551e-01");
    check(sprintf(buf, "%.30f", 0.1), 32);
    check_str(buf, "0.100000000000000005551115123126");
    check(sprintf(buf, "%g", 0.1), 3);
    check_str(buf, "0.1");
    check(sprintf(buf, "%.1f", 0.1), 3);
    check_str(buf, "0.1");
    check(sprintf(buf, "%.0f", 0.1), 1);
    check_str(buf, "0");
    check(sprintf(buf, "%f", 1.0 / 3.0), 8);
    check_str(buf, "0.333333");
    check(sprintf(buf, "%.15e", 1.0 / 3.0), 21);
    check_str(buf, "3.333333333333333e-01");
    check(sprintf(buf, "%g", 1.0 / 3.0), 8);
    check_str(buf, "0.333333");
    check(sprintf(buf, "%.3g", 1.0 / 3.0), 5);
    check_str(buf, "0.333");
    check(sprintf(buf, "%10.4f", 1.0 / 3.0), 10);
    check_str(buf, "    0.3333");
    check(sprintf(buf, "%-12.4e|", 1.0 / 3.0), 13);
    check_str(buf, "3.3333e-01  |");
    check(sprintf(buf, "%.5f", 2.0 / 3.0), 7);
    check_str(buf, "0.66667");
    check(sprintf(buf, "%.0e", 2.0 / 3.0), 5);
    check_str(buf, "7e-01");
    check(sprintf(buf, "%G", 2.0 / 3.0), 8);
    check_str(buf, "0.666667");
    check(sprintf(buf, "%.1g", 2.0 / 3.0), 3);
    check_str(buf, "0.7");
}

/* printf's floating conversions, cases 41 to 60 */
void print3(void)
{
    check(sprintf(buf, "%f", 123.456), 10);
    check_str(buf, "123.456000");
    check(sprintf(buf, "%e", 123.456), 12);
    check_str(buf, "1.234560e+02");
    check(sprintf(buf, "%g", 123.456), 7);
    check_str(buf, "123.456");
    check(sprintf(buf, "%.2f", 123.456), 6);
    check_str(buf, "123.46");
    check(sprintf(buf, "%.1e", 123.456), 7);
    check_str(buf, "1.2e+02");
    check(sprintf(buf, "%.10g", 123.456), 7);
    check_str(buf, "123.456");
    check(sprintf(buf, "%12.3E", 123.456), 12);
    check_str(buf, "   1.235E+02");
    check(sprintf(buf, "%g", 1e-5), 5);
    check_str(buf, "1e-05");
    check(sprintf(buf, "%f", 1e-5), 8);
    check_str(buf, "0.000010");
    check(sprintf(buf, "%e", 1e-5), 12);
    check_str(buf, "1.000000e-05");
    check(sprintf(buf, "%.3g", 1e-5), 5);
    check_str(buf, "1e-05");
    check(sprintf(buf, "%#g", 1e-5), 11);
    check_str(buf, "1.00000e-05");
    check(sprintf(buf, "%g", 0.0001234), 9);
    check_str(buf, "0.0001234");
    check(sprintf(buf, "%.2g", 0.0001234), 7);
    check_str(buf, "0.00012");
    check(sprintf(buf, "%G", 0.0001234), 9);
    check_str(buf, "0.0001234");
    check(sprintf(buf, "%g", 1.5e-7), 7);
    check_str(buf, "1.5e-07");
    check(sprintf(buf, "%.10f", 1.5e-7), 12);
    check_str(buf, "0.0000001500");
    check(sprintf(buf, "%f", 12345678.9), 15);
    check_str(buf, "12345678.900000");
    check(sprintf(buf, "%g", 12345678.9), 11);
    check_str(buf, "1.23457e+07");
    check(sprintf(buf, "%.9g", 12345678.9), 10);
    check_str(buf, "12345678.9");
}

/* printf's floating conversions, cases 61 to 80 */
void print4(void)
{
    check(sprintf(buf, "%e", 12345678.9), 12);
    check_str(buf, "1.234568e+07");
    check(sprintf(buf, "%.0f", 12345678.9), 8);
    check_str(buf, "12345679");
    check(sprintf(buf, "%f", 1e21), 29);
    check_str(buf, "1000000000000000000000.000000");
    check(sprintf(buf, "%g", 1e21), 5);
    check_str(buf, "1e+21");
    check(sprintf(buf, "%e", 1e21), 12);
    check_str(buf, "1.000000e+21");
    check(sprintf(buf, "%e", 1e100), 13);
    check_str(buf, "1.000000e+100");
    check(sprintf(buf, "%g", 1e100), 6);
    check_str(buf, "1e+100");
    check(sprintf(buf, "%.3e", 1e100), 10);
    check_str(buf, "1.000e+100");
    check(sprintf(buf, "%e", 1e-300), 13);
    check_str(buf, "1.000000e-300");
    check(sprintf(buf, "%g", 1e-300), 6);
    check_str(buf, "1e-300");
    check(sprintf(buf, "%e", 5e-324), 13);
    check_str(buf, "4.940656e-324");
    check(sprintf(buf, "%g", 5e-324), 12);
    check_str(buf, "4.94066e-324");
    check(sprintf(buf, "%.17g", 5e-324), 23);
    check_str(buf, "4.9406564584124654e-324");
    check(sprintf(buf, "%e", 1.7976931348623157e308), 13);
    check_str(buf, "1.797693e+308");
    check(sprintf(buf, "%.17g", 1.7976931348623157e308), 23);
    check_str(buf, "1.7976931348623157e+308");
    check(sprintf(buf, "%g", 1.7976931348623157e308), 12);
    check_str(buf, "1.79769e+308");
    check(sprintf(buf, "%.0f", 0.5), 1);
    check_str(buf, "0");
    check(sprintf(buf, "%.0e", 0.5), 5);
    check_str(buf, "5e-01");
    check(sprintf(buf, "%.0f", 1.5), 1);
    check_str(buf, "2");
    check(sprintf(buf, "%.0f", 2.5), 1);
    check_str(buf, "2");
}

/* printf's floating conversions, cases 81 to 100 */
void print5(void)
{
    check(sprintf(buf, "%.0e", 2.5), 5);
    check_str(buf, "2e+00");
    check(sprintf(buf, "%.1f", 1.25), 3);
    check_str(buf, "1.2");
    check(sprintf(buf, "%.2f", 0.125), 4);
    check_str(buf, "0.12");
    check(sprintf(buf, "%.1e", 0.125), 7);
    check_str(buf, "1.2e-01");
    check(sprintf(buf, "%.2f", 9.9999999), 5);
    check_str(buf, "10.00");
    check(sprintf(buf, "%g", 9.9999999), 2);
    check_str(buf, "10");
    check(sprintf(buf, "%.3e", 9.9999999), 9);
    check_str(buf, "1.000e+01");
    check(sprintf(buf, "%.1f", 99.95), 5);
    check_str(buf, "100.0");
    check(sprintf(buf, "%.3g", 99.95), 3);
    check_str(buf, "100");
    check(sprintf(buf, "%g", 100.0), 3);
    check_str(buf, "100");
    check(sprintf(buf, "%e", 100.0), 12);
    check_str(buf, "1.000000e+02");
    check(sprintf(buf, "%#.0e", 100.0), 6);
    check_str(buf, "1.e+02");
    check(sprintf(buf, "%g", 1e6), 5);
    check_str(buf, "1e+06");
    check(sprintf(buf, "%f", 1e6), 14);
    check_str(buf, "1000000.000000");
    check(sprintf(buf, "%g", 123456.0), 6);
    check_str(buf, "123456");
    check(sprintf(buf, "%.5g", 123456.0), 10);
    check_str(buf, "1.2346e+05");
    check(sprintf(buf, "%g", 1234567.0), 11);
    check_str(buf, "1.23457e+06");
    check(sprintf(buf, "%G", 1234567.0), 11);
    check_str(buf, "1.23457E+06");
    check(sprintf(buf, "%g", -9.5e-10), 8);
    check_str(buf, "-9.5e-10");
    check(sprintf(buf, "%12.2e|", -9.5e-10), 13);
    check_str(buf, "   -9.50e-10|");
}

/* the rest of printf: float and long double arguments, infinities and
 * NaNs, the largest %f, '*', a stream, and '#' on o and x */
void print_more(void)
{
    double inf;
    double nan;
    FILE *f;
    float x;
    long double ld;

    x = 0.1f;
    check(sprintf(buf, "%.10f", x), 12);          /* promoted to double */
    check_str(buf, "0.1000000015");
    ld = 2.5L;
    check(sprintf(buf, "%Lf|%Le", ld, ld), 21);
    check_str(buf, "2.500000|2.500000e+00");
    inf = 1e308;
    inf = inf * 10;
    nan = inf - inf;
    check(sprintf(buf, "%f %e %g %E %G", inf, -inf, inf, inf, -inf), 21);
    check_str(buf, "inf -inf inf INF -INF");
    check(sprintf(buf, "[%6f] [%-6f] [%+f] [%06f]", inf, inf, inf, inf), 33);
    check_str(buf, "[   inf] [inf   ] [+inf] [   inf]");
    sprintf(buf, "%f", nan);
    check(strcmp(buf, "nan") == 0 || strcmp(buf, "-nan") == 0, 1);
    sprintf(buf, "%F", -nan);
    check(strcmp(buf, "NAN") == 0 || strcmp(buf, "-NAN") == 0, 1);
    check(sprintf(buf, "%f", 1e308), 316);
    check_str(buf,
        "1000000000000000010979063629440455417404923096773118463368106829"
        "0315758540491149153716332897849468889906124966972117251561159028"
        "3743140088328307009198146046031271664502933027185697489699588559"
        "0433383844661650011784268976262129451776280911957867074581227839"
        "70171784415105291802893207873272974885715430223118336.000000");
    check(sprintf(buf, "%*.*f|%-*.*e|", 9, 2, 3.14159, 12, 3, -3.14159), 23);
    check_str(buf, "     3.14|-3.142e+00  |");
    check(sprintf(buf, "%#o %#o %#x %#X %#x %#5o %#08x", 8, 0, 255, 255, 0, 8, 26), 32);
    check_str(buf, "010 0 0xff 0XFF 0   010 0x00001a");
    check(sprintf(buf, "%d %s %.2f %c %g", 42, "mid", 2.375, 'z', 0.5), 17);
    check_str(buf, "42 mid 2.38 z 0.5");
    f = fopen("fpio.txt", "w+");
    check(f != NULL, 1);
    if (f == NULL)
        return;
    check(fprintf(f, "%.3f|%g|%e", 2.0 / 3.0, 1e-10, 12.5), 24);
    rewind(f);
    fgets(buf, sizeof buf, f);
    check_str(buf, "0.667|1e-10|1.250000e+01");
    fclose(f);
    remove("fpio.txt");
}

/* scanf's e f g, with l and L for double and none for float */
void scan(void)
{
    double d;
    double d2;
    double d3;
    float g;
    float g2;
    long double ld;
    int n;
    char rest[8];

    check(sscanf("3.14159 -2.5e3 .5 x", "%lf %f %Lf %s", &d, &g, &ld, rest), 4);
    checkd(d, 0x400921F9UL, 0xF01B866EUL);
    checkf(g, 0xC51C4000UL);
    checkd(ld, 0x3FE00000UL, 0);
    check_str(rest, "x");
    check(sscanf("123.456", "%5lf%n", &d, &n), 1);        /* a width */
    checkd(d, 0x405ED999UL, 0x9999999AUL);
    check(n, 5);
    check(sscanf("0.1 0.1", "%f %lf", &g, &d), 2);        /* a float from the decimal itself */
    checkf(g, 0x3DCCCCCDUL);
    checkd(d, 0x3FB99999UL, 0x9999999AUL);
    check(sscanf("16777217", "%g", &g), 1);
    checkf(g, 0x4B800000UL);
    check(sscanf("1e40 -1e400", "%e %le", &g, &d), 2);   /* out of range: infinities */
    checkf(g, 0x7F800000UL);
    checkd(d, 0xFFF00000UL, 0);
    check(sscanf("-0 1.5E3", "%lf %lG", &d, &d2), 2);
    checkd(d, 0x80000000UL, 0);
    checkd(d2, 0x40977000UL, 0);
    check(sscanf("1e+x", "%lf", &d), 0);                 /* "1e+" is no number */
    check(sscanf("abc", "%lf", &d), 0);
    check(sscanf("", "%lf", &d), EOF);
    check(sscanf(" 7.25 8", "%*f %lf", &d), 1);          /* suppressed */
    checkd(d, 0x40200000UL, 0);
    check(sscanf("2.5,-.5;", "%lf,%lf;%n", &d2, &d3, &n), 2);
    checkd(d2, 0x40040000UL, 0);
    checkd(d3, 0xBFE00000UL, 0);
    check(n, 8);
    check(sscanf("1.5 2.5", "%f %f", &g, &g2), 2);
    checkf(g2, 0x40200000UL);
}

/* strtod and atof */
void conv(void)
{
    char *end;
    char *s;
    int i;

    s = "  1.5e3xyz";
    checkd(strtod(s, &end), 0x40977000UL, 0);
    check(end - s, 7);
    s = "1e";
    checkd(strtod(s, &end), 0x3FF00000UL, 0);           /* the exponent is incomplete */
    check(end - s, 1);
    s = "-1e+z";
    checkd(strtod(s, &end), 0xBFF00000UL, 0);
    check(end - s, 2);
    s = " .";
    checkd(strtod(s, &end), 0, 0);
    check(end == s, 1);                                   /* no conversion */
    s = "abc";
    strtod(s, &end);
    check(end == s, 1);
    checkd(strtod("-.5", NULL), 0xBFE00000UL, 0);
    checkd(strtod("123456789012345678901234567890", NULL), 0x45F8EE90UL, 0xFF6C373EUL);
    errno = 0;
    checkd(strtod("4.9e-324", NULL), 0, 1);             /* the least subnormal */
    check(errno, 0);
    checkd(strtod("0.000", NULL), 0, 0);
    check(errno, 0);
    checkd(strtod("1e400", NULL), 0x7FF00000UL, 0);      /* HUGE_VAL */
    check(errno, ERANGE);
    errno = 0;
    checkd(strtod("-1e-400", &end), 0x80000000UL, 0);
    check(errno, ERANGE);
    check(*end, 0);
    checkd(atof("2.5"), 0x40040000UL, 0);
    checkd(atof("1.0000000000000002"), 0x3FF00000UL, 1);
    /* 1 + 2^-53 exactly is a tie and rounds to even (1); with a 1 after 900
     * zeros beyond it, above the tie: up (only the dropped digits say so) */
    strcpy(big, "1.00000000000000011102230246251565404236316680908203125");
    checkd(atof(big), 0x3FF00000UL, 0);
    for (i = 0; i < 900; i++)
        big[55 + i] = '0';
    big[955] = 0;
    checkd(atof(big), 0x3FF00000UL, 0);
    big[955] = '1';
    big[956] = 0;
    checkd(atof(big), 0x3FF00000UL, 1);
}

int main(void)
{
    print1();
    print2();
    print3();
    print4();
    print5();
    print_more();
    scan();
    conv();
    return finish();
}
