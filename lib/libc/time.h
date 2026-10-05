/* time.h - dates and times (C89 4.12; c89_spec.md 15), implemented in
 * time.c. time reads the Agon's real-time clock; MOS has no time zones, so
 * its time is taken to be local time and UTC alike, and tm_isdst is -1.
 * time returns -1 when the clock has never been set. clock counts MOS's
 * timer, in hundredths of a second, from the program's start. time_t
 * covers 1901-12-13 to 2038-01-19. */

#ifndef _TIME_H
#define _TIME_H

#define NULL ((void *)0)

#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned int size_t;
#endif

#define CLOCKS_PER_SEC 100

typedef long clock_t;
typedef long time_t;            /* seconds since 1970-01-01 00:00:00 */

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

clock_t clock(void);
double difftime(time_t time1, time_t time0);
time_t time(time_t *timer);
time_t mktime(struct tm *timeptr);
struct tm *gmtime(const time_t *timer);
struct tm *localtime(const time_t *timer);
char *asctime(const struct tm *timeptr);
char *ctime(const time_t *timer);
size_t strftime(char *s, size_t maxsize, const char *format, const struct tm *timeptr);

#endif
