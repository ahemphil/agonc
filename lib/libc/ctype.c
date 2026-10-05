/* ctype.c - the "C" locale, the only one (c89_spec.md 15): <ctype.h>'s
 * classes of 7-bit ASCII, where any other value, EOF and negative chars
 * included, is in no class; and <locale.h>.
 *
 * Each class is a range comparison, not the usual 256-entry table indexed
 * by the character, so a value outside 0-255 (a negative char passed
 * straight in, say) is in no class instead of indexing outside a table. */

#include <ctype.h>
#include <locale.h>
#include <limits.h>
#include <string.h>

int isdigit(int c)
{
    return c >= '0' && c <= '9';
}

int isupper(int c)
{
    return c >= 'A' && c <= 'Z';
}

int islower(int c)
{
    return c >= 'a' && c <= 'z';
}

int isalpha(int c)
{
    return isupper(c) || islower(c);
}

int isalnum(int c)
{
    return isalpha(c) || isdigit(c);
}

/* space, \t \n \v \f \r (9 to 13) */
int isspace(int c)
{
    return c == ' ' || (c >= 9 && c <= 13);
}

int isxdigit(int c)
{
    return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int isprint(int c)
{
    return c >= 32 && c <= 126;
}

int iscntrl(int c)
{
    return (c >= 0 && c <= 31) || c == 127;
}

int isgraph(int c)
{
    return c >= 33 && c <= 126;
}

int ispunct(int c)
{
    return isgraph(c) && !isalnum(c);
}

/* An ASCII letter's two cases differ by 32 (bit 5). */
int toupper(int c)
{
    return islower(c) ? c - 32 : c;
}

int tolower(int c)
{
    return isupper(c) ? c + 32 : c;
}

/* ---- <locale.h> ------------------------------------------------------------------ */

/* "C", and "" (the same), for any category; NULL for anything else, or an
 * unknown category. A null locale asks which one is in use. */
char *setlocale(int category, const char *locale)
{
    if (category < LC_ALL || category > LC_TIME)
        return NULL;
    if (locale == NULL || locale[0] == 0 || strcmp(locale, "C") == 0)
        return "C";
    return NULL;
}

/* The "C" locale's lconv (C89 4.4.2.1): a decimal point of ".", every other
 * string empty, every char member CHAR_MAX, meaning "not available". */
static struct lconv c_locale = { ".", "", "", "", "", "", "", "", "", "", CHAR_MAX, CHAR_MAX, CHAR_MAX,
                                 CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX };

struct lconv *localeconv(void)
{
    return &c_locale;
}
