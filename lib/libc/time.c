/* time.c - <time.h> (c89_spec.md 15).
 *
 * A time_t is a long count of seconds since 1970-01-01 00:00:00, so it
 * covers 1901-12-13 to 2038-01-19. Dates convert through a count of days
 * since 1970 with Howard Hinnant's days_from_civil and civil_from_days,
 * exact for the proleptic Gregorian calendar. MOS has no time zones: the
 * clock is taken to be local time and UTC alike, so gmtime and localtime
 * agree and tm_isdst is -1.
 *
 * Both date algorithms count years from 1 March, not 1 January: the leap
 * day then falls at the very end of the year, so the months March to
 * February have lengths that one linear formula gives, whatever the year.
 * The Gregorian calendar repeats every 400 years, an "era" of exactly
 * 146097 days, so a date splits into an era and a year of era (0-399).
 *
 * The sections: the conversions between time_t and calendar fields, the
 * clocks (clock, time, the RTC text), mktime, then strftime and asctime.
 */

#include <time.h>
#include <string.h>
#include <agon/mos.h>

extern unsigned long __clock0;          /* crt0: MOS's timer at startup */

static struct tm result;                /* gmtime's and localtime's */

static char *day_names[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
static char *month_names[12] = { "January", "February", "March", "April", "May", "June", "July", "August",
                                 "September", "October", "November", "December" };

/* The days from 1970-01-01 to y-m-d (m 1-12, d 1-31, or beyond: the
 * formula counts on). */
static long days_from_civil(long y, int m, long d)
{
    long era;
    long yoe;
    long doy;

    /* January and February belong to the March-based year before */
    if (m <= 2)
        y--;
    /* floor division by 400: C's / truncates towards zero, so a negative
     * year is moved down first */
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    /* the day of the March-based year: month 0 is March, and
     * (153 * month + 2) / 5 is the days before that month (31, 30, 31,
     * 30, 31, then the same five again, then January) */
    doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    /* whole eras, whole years with their leap days (every 4th year, but
     * not every 100th; the 400th year's leap day ends the era, so it is
     * in the 146097), the day of the year, and 719468, the days from
     * 0000-03-01 to 1970-01-01 */
    return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}

/* The calendar fields of t into tm: the time of day, then the date by
 * civil_from_days, days_from_civil run backwards. */
static void fields(time_t t, struct tm *tm)
{
    long days;
    long secs;
    long z;
    long era;
    long doe;
    long yoe;
    long doy;
    long mp;
    long y;
    int m;

    /* floor division: a time before 1970 gives a negative remainder,
     * which is moved into the day before */
    days = t / 86400;
    secs = t % 86400;
    if (secs < 0) {
        secs = secs + 86400;
        days--;
    }
    tm->tm_hour = (int)(secs / 3600);
    tm->tm_min = (int)(secs / 60 % 60);
    tm->tm_sec = (int)(secs % 60);
    /* 1970-01-01 was a Thursday (4); days % 7 is -6 to 6, so 11 = 4 + 7
     * keeps the sum positive */
    tm->tm_wday = (int)((days % 7 + 11) % 7);
    /* z counts from 0000-03-01; the era and the day of the era (doe) */
    z = days + 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = z - era * 146097;
    /* the year of the era: taking out the leap days before doe (one per
     * 1460 days, less one per 36524, plus the era's last day) leaves 365
     * days to every year */
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    /* the March-based month, inverting days_from_civil's (153 * mp + 2) / 5 */
    mp = (5 * doy + 2) / 153;
    tm->tm_mday = (int)(doy - (153 * mp + 2) / 5 + 1);
    /* back to January-based months and years */
    m = (int)(mp < 10 ? mp + 3 : mp - 9);
    if (m <= 2)
        y++;
    tm->tm_mon = m - 1;
    tm->tm_year = (int)(y - 1900);
    tm->tm_yday = (int)(days - days_from_civil(y, 1, 1));
    tm->tm_isdst = -1;
}

/* Hundredths of a second since the program started: MOS's timer, the
 * first 4 bytes of its system variables, less the value crt0 saved. The
 * unsigned subtraction stays right when the timer wraps. */
clock_t clock(void)
{
    return (clock_t)(*(unsigned long *)mos_sysvars() - __clock0);
}

/* time1 - time0 in seconds, exactly: a long converts to a double exactly,
 * and the difference of two needs at most 33 bits. */
double difftime(time_t time1, time_t time0)
{
    return (double)time1 - (double)time0;
}

/* The time in the real-time clock's text from MOS, "Sun, 27/09/2026
 * 10:54:26" (its six numbers in that order, day first); (time_t)-1 for
 * anything else, including a clock never set: MOS then reports day 0 of
 * 1980 (as the emulator's always does, which is why the library's tests
 * call this directly). */
time_t __rtc_time(const char *text)
{
    long v[6];
    int n;
    const char *p;

    /* each run of digits is one number: day, month, year, hour, minute,
     * second; the p-- leaves the loop's p++ to step past the run */
    n = 0;
    for (p = text; *p && n < 6; p++) {
        if (*p >= '0' && *p <= '9') {
            v[n] = 0;
            while (*p >= '0' && *p <= '9') {
                v[n] = v[n] * 10 + (*p - '0');
                p++;
            }
            n++;
            p--;
        }
    }
    if (n < 6 || v[0] < 1 || v[0] > 31 || v[1] < 1 || v[1] > 12)
        return -1;
    return (days_from_civil(v[2], (int)v[1], v[0]) * 24 + v[3]) * 3600 + v[4] * 60 + v[5];
}

/* The real-time clock (__rtc_time). */
time_t time(time_t *timer)
{
    char buf[64];
    time_t t;

    memset(buf, 0, sizeof buf);
    mos_getrtc(buf);
    t = __rtc_time(buf);
    if (timer != NULL)
        *timer = t;
    return t;
}

/* The fields, any of them out of range, made a time (C89 4.12.2.3), and
 * set again normalised, with tm_wday and tm_yday; (time_t)-1 if the time
 * is outside time_t's range. */
time_t mktime(struct tm *tm)
{
    long y;
    long mon;
    long days;
    time_t t;

    /* whole years out of the month, then the month into 0-11 (floor, as
     * in fields); the day, hours, minutes and seconds then simply add on,
     * since days_from_civil counts past a month's end */
    mon = tm->tm_mon;
    y = tm->tm_year + 1900L + mon / 12;
    mon = mon % 12;
    if (mon < 0) {
        mon = mon + 12;
        y--;
    }
    days = days_from_civil(y, (int)mon + 1, 1) + tm->tm_mday - 1;
    t = tm->tm_hour * 3600L + tm->tm_min * 60L + tm->tm_sec;
    days = days + t / 86400;
    t = t % 86400;
    /* 24855 days is just under 2^31 seconds. At the two edge days the
     * result can still overflow; the arithmetic wraps, and a result whose
     * sign differs from the days' shows it */
    if (days < -24856 || days > 24855)
        return -1;
    t = days * 86400 + t;
    if ((days > 0 && t < 0) || (days < 0 && t > 0))
        return -1;                      /* beyond 2038 or before 1901 */
    fields(t, tm);
    return t;
}

struct tm *gmtime(const time_t *timer)
{
    fields(*timer, &result);
    return &result;
}

struct tm *localtime(const time_t *timer)
{
    return gmtime(timer);
}

/* ---- text -------------------------------------------------------------------------- */

/* Where strftime's characters go: out, with room for max - 1 of them and
 * the NUL. n counts every character, stored or not, so strftime can tell
 * afterwards whether all fitted. */
struct text {
    char *out;
    size_t n;
    size_t max;
};

/* Up to len characters of s (-1: all of it). */
static void add(struct text *t, const char *s, int len)
{
    while (*s && len != 0) {
        if (t->n + 1 < t->max)
            t->out[t->n] = *s;
        t->n++;                         /* counted even when it does not fit */
        s++;
        len--;
    }
}

/* v in at least width digits, padded with pad ('0' or ' '). The digits
 * are made from the right, least significant first, into d's end; v is
 * taken to be non-negative, as a normalised tm's fields are. */
static void num(struct text *t, int v, int width, int pad)
{
    char d[8];
    int i;

    i = 7;
    d[i] = 0;
    do {
        i--;
        d[i] = '0' + v % 10;
        v = v / 10;
        width--;
    } while (v > 0 && i > 0);
    while (width > 0 && i > 0) {
        i--;
        d[i] = pad;
        width--;
    }
    add(t, d + i, -1);
}

static void format(struct text *t, const char *f, const struct tm *tm);

/* One conversion (C89 4.12.3.5), in the "C" locale. */
static void conversion(struct text *t, int c, const struct tm *tm)
{
    int h;

    switch (c) {
    case 'a':
        add(t, day_names[tm->tm_wday % 7], 3);
        break;
    case 'A':
        add(t, day_names[tm->tm_wday % 7], -1);
        break;
    case 'b':
        add(t, month_names[tm->tm_mon % 12], 3);
        break;
    case 'B':
        add(t, month_names[tm->tm_mon % 12], -1);
        break;
    case 'c':
        format(t, "%a %b %e %H:%M:%S %Y", tm);
        break;
    case 'd':
        num(t, tm->tm_mday, 2, '0');
        break;
    case 'e':                           /* (for %c: the day, space-padded, as asctime) */
        num(t, tm->tm_mday, 2, ' ');
        break;
    case 'H':
        num(t, tm->tm_hour, 2, '0');
        break;
    case 'I':
        h = tm->tm_hour % 12;
        num(t, h == 0 ? 12 : h, 2, '0');
        break;
    case 'j':
        num(t, tm->tm_yday + 1, 3, '0');
        break;
    case 'm':
        num(t, tm->tm_mon + 1, 2, '0');
        break;
    case 'M':
        num(t, tm->tm_min, 2, '0');
        break;
    case 'p':
        add(t, tm->tm_hour < 12 ? "AM" : "PM", -1);
        break;
    case 'S':
        num(t, tm->tm_sec, 2, '0');
        break;
    case 'U':                           /* weeks starting on Sunday */
        num(t, (tm->tm_yday + 7 - tm->tm_wday) / 7, 2, '0');
        break;
    case 'w':
        num(t, tm->tm_wday, 1, '0');
        break;
    case 'W':                           /* weeks starting on Monday */
        num(t, (tm->tm_yday + 7 - (tm->tm_wday + 6) % 7) / 7, 2, '0');
        break;
    case 'x':
        format(t, "%m/%d/%y", tm);
        break;
    case 'X':
        format(t, "%H:%M:%S", tm);
        break;
    case 'y':
        num(t, (tm->tm_year + 1900) % 100, 2, '0');
        break;
    case 'Y':
        num(t, tm->tm_year + 1900, 1, '0');
        break;
    case 'Z':                           /* no time zone is known: nothing */
        break;
    case '%':
        add(t, "%", 1);
        break;
    default:                            /* not C89's: as written */
        add(t, "%", 1);
        /* c's first byte is its low byte (little-endian), the character */
        add(t, (char *)&c, 1);
        break;
    }
}

/* Ordinary characters are copied; a '%' at the format's very end is too. */
static void format(struct text *t, const char *f, const struct tm *tm)
{
    while (*f) {
        if (*f == '%' && f[1]) {
            conversion(t, f[1], tm);
            f = f + 2;
        } else {
            add(t, f, 1);
            f++;
        }
    }
}

/* The count of characters written, or 0 if they (with the terminator) do
 * not fit in maxsize, when s is left indeterminate. */
size_t strftime(char *s, size_t maxsize, const char *f, const struct tm *tm)
{
    struct text t;

    t.out = s;
    t.n = 0;
    t.max = maxsize;
    format(&t, f, tm);
    if (t.n + 1 > maxsize)
        return 0;
    s[t.n] = 0;
    return t.n;
}

/* "Sun Sep 16 01:03:52 1973\n" (C89 4.12.3.1). */
char *asctime(const struct tm *tm)
{
    static char buf[32];

    strftime(buf, sizeof buf, "%a %b %e %H:%M:%S %Y\n", tm);
    return buf;
}

char *ctime(const time_t *timer)
{
    return asctime(localtime(timer));
}
