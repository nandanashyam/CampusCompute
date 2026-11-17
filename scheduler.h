#ifndef SCHEDULER_H
#define SCHEDULER_H
#include "job.h"

#define MAX_SEGS 4096

typedef struct { int job_id; int start; int end; } Segment;   /* job_id 0 = CPU idle */

typedef struct {
    const char *name;
    char rule[160];
    int quantum;                    /* 0 when not Round Robin */
    Job jobs[MAX_JOBS];
    int count;
    Segment timeline[MAX_SEGS];
    int nseg;
    double avg_tat, avg_wt;
} SchedResult;

int  sched_fcfs(const JobSet *in, SchedResult *out);
int  sched_rr(const JobSet *in, int quantum, SchedResult *out);
int  sched_priority(const JobSet *in, SchedResult *out);
void sched_print(const SchedResult *r);

/* Menu / CLI entry points (use g_jobs). quantum <= 0 means "ask the user". Return 0 on success. */
int run_fcfs(void);
int run_rr(int quantum);
int run_priority(void);
int run_compare(int quantum);
#endif
