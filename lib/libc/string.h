/* string.h - implemented in string.c. Comparisons are by unsigned byte
 * value; in the "C" locale strcoll is strcmp and strxfrm a copy. */

#ifndef _STRING_H
#define _STRING_H

#define NULL ((void *)0)

#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned int size_t;
#endif

size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *d, const char *s);
char *strncpy(char *d, const char *s, size_t n);
char *strcat(char *d, const char *s);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
void *memchr(const void *s, int c, size_t n);
char *strncat(char *d, const char *s, size_t n);
int strcoll(const char *a, const char *b);
size_t strxfrm(char *d, const char *s, size_t n);
size_t strspn(const char *s, const char *set);
size_t strcspn(const char *s, const char *set);
char *strpbrk(const char *s, const char *set);
char *strstr(const char *s, const char *find);
char *strtok(char *s, const char *sep);
char *strerror(int errnum);

#endif
