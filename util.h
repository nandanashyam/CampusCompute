#ifndef UTIL_H
#define UTIL_H
#include <stddef.h>

/* Read one line from stdin (newline stripped). Returns 0 on success, -1 on EOF. */
int read_line(const char *prompt, char *buf, size_t size);
/* Parse a whole number in [min,max]. Returns 0 on success, -1 if malformed/out of range. */
int parse_int(const char *s, int min, int max, int *out);
/* Prompt until a valid integer in [min,max] is typed. Returns 0 on success, -1 on EOF. */
int read_int_range(const char *prompt, int min, int max, int *out);
void sleep_ms(int ms);
#endif
