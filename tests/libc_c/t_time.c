/* t_time.c - <time.h> (M12 step 6): conversions at known instants (the
 * epoch, a leap day, the ends of a 32-bit time_t), mktime's normalising,
 * strftime's every conversion, and the live clocks: time() reads the
 * real-time clock (a date after 2024, or -1 if it has not been set, as in
 * the emulator), clock() counts up from the start. */

#include <time.h>
#include <string.h>
#include <limits.h>
#include "check.h"

char buf[80];

time_t __rtc_time(const char *text);    /* time.c: time()'s reading of MOS's text */

void check_tm(struct tm *tm, int y, int mon, int d, int h, int mi, int s, int wday, int yday)
{
    check(tm->tm_year + 1900, y);
    check(tm->tm_mon + 1, mon);
    check(tm->tm_mday, d);
    check(tm->tm_hour, h);
    check(tm->tm_min, mi);
    check(tm->tm_sec, s);
    check(tm->tm_wday, wday);
    check(tm->tm_yday, yday);
}

void test_convert(void)
{
    time_t t;
    struct tm tm;
    long k;
    int ok;

    t = 0;
    check_tm(gmtime(&t), 1970, 1, 1, 0, 0, 0, 4, 0);        /* a Thursday */
    check(gmtime(&t)->tm_isdst, -1);
    check_str(asctime(gmtime(&t)), "Thu Jan  1 00:00:00 1970\n");
    t = 951782400L;
    check_tm(gmtime(&t), 2000, 2, 29, 0, 0, 0, 2, 59);      /* a leap day */
    t = LONG_MAX;
    check_tm(gmtime(&t), 2038, 1, 19, 3, 14, 7, 2, 18);
    t = -1;
    check_tm(gmtime(&t), 1969, 12, 31, 23, 59, 59, 3, 364);
    t = LONG_MIN;
    check_tm(gmtime(&t), 1901, 12, 13, 20, 45, 52, 5, 346);
    check_str(ctime(&t), "Fri Dec 13 20:45:52 1901\n");
    /* mktime: fields out of range are normalised */
    memset(&tm, 0, sizeof tm);
    tm.tm_year = 100;
    tm.tm_mday = 32;                    /* January 32nd */
    check(mktime(&tm) == 949363200L, 1);
    check_tm(&tm, 2000, 2, 1, 0, 0, 0, 2, 31);
    memset(&tm, 0, sizeof tm);
    tm.tm_year = 99;
    tm.tm_mon = 13;                     /* the 14th month of 1999 */
    tm.tm_mday = 29;
    check(mktime(&tm) == 951782400L, 1);
    memset(&tm, 0, sizeof tm);
    tm.tm_year = 100;
    tm.tm_mday = 1;
    tm.tm_sec = -1;                     /* a second before 2000 */
    check(mktime(&tm) == 946684799L, 1);
    check_tm(&tm, 1999, 12, 31, 23, 59, 59, 5, 364);
    tm.tm_year = 200;                   /* 2100: beyond time_t */
    check(mktime(&tm) == -1L, 1);
    /* mktime undoes gmtime across the range */
    ok = 1;
    for (k = -39; k <= 39; k++) {
        t = k * 53687091L + 12345;
        tm = *gmtime(&t);
        if (mktime(&tm) != t)
            ok = 0;
    }
    check(ok, 1);
}

void test_strftime(void)
{
    struct tm tm;

    memset(&tm, 0, sizeof tm);
    tm.tm_year = 100;
    tm.tm_mon = 1;
    tm.tm_mday = 29;
    tm.tm_hour = 13;
    tm.tm_min = 5;
    tm.tm_sec = 9;
    mktime(&tm);                        /* Tuesday, day 59 */
    check((int)strftime(buf, sizeof buf, "%a %A %b %B", &tm), 24);
    check_str(buf, "Tue Tuesday Feb February");
    strftime(buf, sizeof buf, "%d %H %I %j %m %M %p %S", &tm);
    check_str(buf, "29 13 01 060 02 05 PM 09");
    strftime(buf, sizeof buf, "%U %w %W %y %Y %% [%Z]", &tm);
    check_str(buf, "09 2 09 00 2000 % []");
    strftime(buf, sizeof buf, "%x %X|%c", &tm);
    check_str(buf, "02/29/00 13:05:09|Tue Feb 29 13:05:09 2000");
    tm.tm_hour = 0;
    strftime(buf, sizeof buf, "%I %p", &tm);
    check_str(buf, "12 AM");
    check((int)strftime(buf, 5, "%Y", &tm), 4);
    check((int)strftime(buf, 4, "%Y", &tm), 0);     /* no room for the terminator */
    check((int)strftime(buf, 10, "", &tm), 0);
    check(buf[0], 0);
}

void test_clocks(void)
{
    time_t t;
    time_t u;
    clock_t c;
    long spin;

    t = time(NULL);
    check(t == -1 || t > 1704067200L, 1);   /* unset (the emulator's), or after 2024 */
    u = 0;
    check(time(&u) == u, 1);
    check(t == -1 ? u == -1 : u >= t, 1);
    check(__rtc_time("Sun, 27/09/2026 10:54:26") == 1790506466L, 1);
    check(__rtc_time("Sat, 01/01/2000 00:00:00") == 946684800L, 1);
    check(__rtc_time("Sun, 00/01/1980 00:00:00") == -1L, 1);   /* never set */
    check(__rtc_time("") == -1L, 1);
    c = clock();
    check(c >= 0 && c < 1000, 1);       /* hundredths since the start */
    check(CLOCKS_PER_SEC, 100);
    for (spin = 0; spin < 2000000L && clock() == c; spin++)
        ;
    check(clock() > c, 1);              /* it moves */
}

int main(void)
{
    test_convert();
    test_strftime();
    test_clocks();
    return finish();
}
