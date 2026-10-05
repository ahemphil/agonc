/* ctype.h - implemented in ctype.c, for the "C" locale's 7-bit ASCII.
 * These are functions, not macros. Only 0-127 can be in a class: EOF,
 * bytes 0x80-0xFF and negative chars are in none, and toupper and tolower
 * return them unchanged. */

#ifndef _CTYPE_H
#define _CTYPE_H

int isalpha(int c);
int isdigit(int c);
int isalnum(int c);
int isspace(int c);
int isupper(int c);
int islower(int c);
int isxdigit(int c);
int isprint(int c);
int iscntrl(int c);
int isgraph(int c);
int ispunct(int c);
int toupper(int c);
int tolower(int c);

#endif
