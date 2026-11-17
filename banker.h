#ifndef BANKER_H
#define BANKER_H
#define BK_MAXP 10
#define BK_MAXR 5

typedef struct {
    int n, m;                         /* processes, resource types */
    int available[BK_MAXR];
    int alloc[BK_MAXP][BK_MAXR];
    int max[BK_MAXP][BK_MAXR];
    int need[BK_MAXP][BK_MAXR];       /* need = max - alloc */
} BankerState;

int banker_compute_need(BankerState *s);                             /* 0 ok, -1 invalid input */
int banker_is_safe(const BankerState *s, int seq[], int verbose);    /* 1 safe, 0 unsafe */
int run_banker_demo(int which);   /* 1 safe case, 2 unsafe case, 3 custom input, 0 = ask.
                                     Returns 1 safe, 0 unsafe, -1 error */
#endif
