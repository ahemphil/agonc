/* streq_c.c - string.c with STR_ASM 0 and every function renamed c_...,
 * for t_streq.c to compare with the library's own build (STR_ASM 1). */

#define STR_ASM 0
#define memchr c_memchr
#define memcmp c_memcmp
#define memcpy c_memcpy
#define memmove c_memmove
#define memset c_memset
#define strcat c_strcat
#define strchr c_strchr
#define strcmp c_strcmp
#define strcoll c_strcoll
#define strcpy c_strcpy
#define strcspn c_strcspn
#define strerror c_strerror
#define strlen c_strlen
#define strncat c_strncat
#define strncmp c_strncmp
#define strncpy c_strncpy
#define strpbrk c_strpbrk
#define strrchr c_strrchr
#define strspn c_strspn
#define strstr c_strstr
#define strtok c_strtok
#define strxfrm c_strxfrm
#include "../../lib/libc/string.c"
