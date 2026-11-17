#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

int read_line(const char *prompt, char *buf, size_t size)
{
    if (prompt) { printf("%s", prompt); fflush(stdout); }
    if (!fgets(buf, (int)size, stdin)) return -1;
    size_t n = strlen(buf);
    if (n > 0 && buf[n - 1] == '\n') {
        buf[n - 1] = '\0';
    } else {                      /* line longer than buffer: discard the rest */
        int c;
        while ((c = getchar()) != '\n' && c != EOF) { }
    }
    return 0;
}

int parse_int(const char *s, int min, int max, int *out)
{
    char *end;
    if (s == NULL || *s == '\0') return -1;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s) return -1;
    while (*end == ' ' || *end == '\t') end++;
    if (*end != '\0') return -1;          /* trailing garbage such as "12abc" */
    if (v < min || v > max) return -1;
    *out = (int)v;
    return 0;
}

int read_int_range(const char *prompt, int min, int max, int *out)
{
    char buf[64];
    for (;;) {
        if (read_line(prompt, buf, sizeof buf) < 0) return -1;
        if (parse_int(buf, min, max, out) == 0) return 0;
        printf("Error: please enter a whole number between %d and %d.\n", min, max);
    }
}

void sleep_ms(int ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) { }
}
