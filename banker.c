#include "banker.h"
#include "logger.h"
#include "util.h"
#include <stdio.h>
#include <string.h>

static void print_vec(const int *v, int m)
{
    printf("(");
    for (int j = 0; j < m; j++) printf("%s%d", j ? "," : "", v[j]);
    printf(")");
}

static void print_state(const BankerState *s)
{
    printf("\nResources: ");
    for (int j = 0; j < s->m; j++) printf("%c ", 'A' + j);
    printf("\nAvailable = "); print_vec(s->available, s->m);
    printf("\n\n%-6s %-12s %-12s %-12s\n", "Proc", "Allocation", "Maximum", "Need");
    printf("---------------------------------------------\n");
    for (int i = 0; i < s->n; i++) {
        printf("P%-5d ", i);
        print_vec(s->alloc[i], s->m);   printf("   ");
        print_vec(s->max[i], s->m);     printf("   ");
        print_vec(s->need[i], s->m);    printf("\n");
    }
}

/* Validate and compute Need = Maximum - Allocation. */
int banker_compute_need(BankerState *s)
{
    if (s->n < 1 || s->n > BK_MAXP || s->m < 1 || s->m > BK_MAXR) {
        printf("Error: need 1-%d processes and 1-%d resource types.\n", BK_MAXP, BK_MAXR);
        return -1;
    }
    for (int j = 0; j < s->m; j++)
        if (s->available[j] < 0) { printf("Error: Available[%c] is negative.\n", 'A' + j); return -1; }
    for (int i = 0; i < s->n; i++)
        for (int j = 0; j < s->m; j++) {
            if (s->alloc[i][j] < 0 || s->max[i][j] < 0) {
                printf("Error: negative value for P%d resource %c.\n", i, 'A' + j);
                return -1;
            }
            if (s->alloc[i][j] > s->max[i][j]) {
                printf("Error: Allocation exceeds Maximum for P%d resource %c (%d > %d).\n",
                       i, 'A' + j, s->alloc[i][j], s->max[i][j]);
                return -1;
            }
            s->need[i][j] = s->max[i][j] - s->alloc[i][j];
        }
    return 0;
}

/* Banker's safety algorithm. seq[] receives the safe sequence (process indices) if safe. */
int banker_is_safe(const BankerState *s, int seq[], int verbose)
{
    int work[BK_MAXR], finish[BK_MAXP] = { 0 }, count = 0;
    memcpy(work, s->available, sizeof work);

    if (verbose) { printf("\nSafety algorithm: Work = Available = "); print_vec(work, s->m); printf("\n"); }

    int progress = 1;
    while (count < s->n && progress) {
        progress = 0;
        for (int i = 0; i < s->n; i++) {
            if (finish[i]) continue;
            int ok = 1;
            for (int j = 0; j < s->m; j++) if (s->need[i][j] > work[j]) { ok = 0; break; }
            if (!ok) continue;
            if (verbose) {
                printf("  P%d can finish: Need ", i); print_vec(s->need[i], s->m);
                printf(" <= Work "); print_vec(work, s->m);
            }
            for (int j = 0; j < s->m; j++) work[j] += s->alloc[i][j];   /* it releases its allocation */
            if (verbose) { printf(" -> Work becomes "); print_vec(work, s->m); printf("\n"); }
            finish[i] = 1;
            seq[count++] = i;
            progress = 1;
        }
    }

    if (count == s->n) {
        if (verbose) {
            printf("\nSAFE STATE. Safe sequence: <");
            for (int i = 0; i < s->n; i++) printf("%sP%d", i ? ", " : "", seq[i]);
            printf(">\n");
        }
        return 1;
    }
    if (verbose) {
        printf("\nUNSAFE STATE: no safe sequence exists. Processes that can never be guaranteed to finish:");
        for (int i = 0; i < s->n; i++) if (!finish[i]) printf(" P%d", i);
        printf("\n  (Work stuck at "); print_vec(work, s->m);
        printf(". Note: an unsafe state does not by itself mean deadlock has occurred.)\n");
    }
    return 0;
}

/* ---------- built-in test cases ---------- */

static BankerState safe_case(void)
{
    /* Classic textbook example: safe sequence <P1,P3,P4,P0,P2> exists. */
    BankerState s = { .n = 5, .m = 3, .available = { 3, 3, 2 },
        .alloc = { {0,1,0}, {2,0,0}, {3,0,2}, {2,1,1}, {0,0,2} },
        .max   = { {7,5,3}, {3,2,2}, {9,0,2}, {2,2,2}, {4,3,3} } };
    return s;
}

static BankerState unsafe_case(void)
{
    /* Same matrices as the guide's worked example, Available = (3,3,2): only P1 can finish. */
    BankerState s = { .n = 3, .m = 3, .available = { 3, 3, 2 },
        .alloc = { {0,1,0}, {2,0,0}, {3,0,2} },
        .max   = { {7,5,3}, {3,2,2}, {9,0,2} } };
    return s;
}

static int read_custom(BankerState *s)
{
    memset(s, 0, sizeof *s);
    if (read_int_range("Number of processes (1-10): ", 1, BK_MAXP, &s->n) < 0) return -1;
    if (read_int_range("Number of resource types (1-5): ", 1, BK_MAXR, &s->m) < 0) return -1;
    char prompt[64];
    for (int j = 0; j < s->m; j++) {
        snprintf(prompt, sizeof prompt, "Available[%c]: ", 'A' + j);
        if (read_int_range(prompt, 0, 1000, &s->available[j]) < 0) return -1;
    }
    for (int i = 0; i < s->n; i++)
        for (int j = 0; j < s->m; j++) {
            snprintf(prompt, sizeof prompt, "Allocation[P%d][%c]: ", i, 'A' + j);
            if (read_int_range(prompt, 0, 1000, &s->alloc[i][j]) < 0) return -1;
        }
    for (int i = 0; i < s->n; i++)
        for (int j = 0; j < s->m; j++) {
            snprintf(prompt, sizeof prompt, "Maximum[P%d][%c]: ", i, 'A' + j);
            if (read_int_range(prompt, 0, 1000, &s->max[i][j]) < 0) return -1;
        }
    return 0;
}

int run_banker_demo(int which)
{
    BankerState s;
    if (which == 0) {
        printf("\n--- Banker's algorithm ---\n 1. Built-in SAFE state\n 2. Built-in UNSAFE state\n"
               " 3. Enter your own matrices\n 0. Back\n");
        if (read_int_range("Choice: ", 0, 3, &which) < 0 || which == 0) return -1;
    }
    if (which == 1)      { s = safe_case();   printf("\n=== Banker's Algorithm: safe-state test ===\n"); }
    else if (which == 2) { s = unsafe_case(); printf("\n=== Banker's Algorithm: unsafe-state test ===\n"); }
    else if (which == 3) { if (read_custom(&s) != 0) return -1; printf("\n=== Banker's Algorithm: custom input ===\n"); }
    else return -1;

    if (banker_compute_need(&s) != 0) { log_event("ERROR", "Banker: invalid input matrices"); return -1; }
    print_state(&s);
    int seq[BK_MAXP];
    int safe = banker_is_safe(&s, seq, 1);
    log_event("INFO", "Banker's check finished: state is %s", safe ? "SAFE" : "UNSAFE");
    return safe;
}
