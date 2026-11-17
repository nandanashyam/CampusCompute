#include "scheduler.h"
#include "logger.h"
#include "util.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>

/* ---------- helpers ---------- */

static void init_result(const JobSet *in, SchedResult *r, const char *name, const char *rule)
{
    memset(r, 0, sizeof *r);
    r->name = name;
    snprintf(r->rule, sizeof r->rule, "%s", rule);
    r->count = in->count;
    for (int i = 0; i < in->count; i++) {
        r->jobs[i] = in->jobs[i];
        r->jobs[i].status = JOB_READY;
        r->jobs[i].remaining_time = r->jobs[i].burst_time;
        r->jobs[i].start_time = -1;
    }
}

/* Append a timeline slice; merges with the previous slice if it is contiguous and the same job. */
static void add_segment(SchedResult *r, int id, int s, int e)
{
    if (e <= s) return;
    if (r->nseg > 0) {
        Segment *l = &r->timeline[r->nseg - 1];
        if (l->job_id == id && l->end == s) { l->end = e; return; }
    }
    if (r->nseg < MAX_SEGS) r->timeline[r->nseg++] = (Segment){ id, s, e };
}

static int arrives_after(const Job *a, const Job *b)
{
    if (a->arrival_time != b->arrival_time) return a->arrival_time > b->arrival_time;
    return a->id > b->id;                     /* tie-break: smaller job ID first */
}

/* ord[] = job indices sorted by (arrival time, job ID) */
static void order_by_arrival(const SchedResult *r, int *ord)
{
    for (int i = 0; i < r->count; i++) ord[i] = i;
    for (int i = 1; i < r->count; i++) {
        int k = ord[i], j = i - 1;
        while (j >= 0 && arrives_after(&r->jobs[ord[j]], &r->jobs[k])) { ord[j + 1] = ord[j]; j--; }
        ord[j + 1] = k;
    }
}

static void finalize(SchedResult *r)
{
    double tat = 0, wt = 0;
    for (int i = 0; i < r->count; i++) {
        Job *j = &r->jobs[i];
        j->turnaround_time = j->completion_time - j->arrival_time;
        j->waiting_time = j->turnaround_time - j->burst_time;
        j->remaining_time = 0;
        j->status = JOB_COMPLETED;
        tat += j->turnaround_time;
        wt += j->waiting_time;
    }
    r->avg_tat = tat / r->count;
    r->avg_wt = wt / r->count;
}

/* ---------- FCFS ---------- */

int sched_fcfs(const JobSet *in, SchedResult *out)
{
    if (in->count <= 0) return -1;
    init_result(in, out, "FCFS", "First-Come First-Served, non-preemptive, run in arrival order");
    int ord[MAX_JOBS], t = 0;
    order_by_arrival(out, ord);
    for (int k = 0; k < out->count; k++) {
        Job *j = &out->jobs[ord[k]];
        if (t < j->arrival_time) {                 /* CPU idle until the job arrives */
            add_segment(out, 0, t, j->arrival_time);
            t = j->arrival_time;
        }
        j->start_time = t;
        add_segment(out, j->id, t, t + j->burst_time);
        t += j->burst_time;
        j->completion_time = t;
    }
    finalize(out);
    return 0;
}

/* ---------- Round Robin ---------- */

int sched_rr(const JobSet *in, int quantum, SchedResult *out)
{
    if (in->count <= 0 || quantum <= 0) return -1;
    init_result(in, out, "Round Robin",
                "Round Robin; a job arriving at the same instant a quantum expires is queued BEFORE the pre-empted job");
    out->quantum = quantum;

    int n = out->count, ord[MAX_JOBS];
    int queue[MAX_JOBS], qh = 0, qt = 0, qn = 0;
    int next = 0, done = 0, t = 0;
    order_by_arrival(out, ord);

    while (done < n) {
        while (next < n && out->jobs[ord[next]].arrival_time <= t) {
            queue[qt] = ord[next++]; qt = (qt + 1) % MAX_JOBS; qn++;
        }
        if (qn == 0) {                              /* nothing ready: idle until next arrival */
            int na = out->jobs[ord[next]].arrival_time;
            add_segment(out, 0, t, na);
            t = na;
            continue;
        }
        int i = queue[qh]; qh = (qh + 1) % MAX_JOBS; qn--;
        Job *j = &out->jobs[i];
        if (j->start_time < 0) j->start_time = t;
        j->status = JOB_RUNNING;
        int run = j->remaining_time < quantum ? j->remaining_time : quantum;
        add_segment(out, j->id, t, t + run);
        t += run;
        j->remaining_time -= run;
        while (next < n && out->jobs[ord[next]].arrival_time <= t) {   /* arrivals during the slice */
            queue[qt] = ord[next++]; qt = (qt + 1) % MAX_JOBS; qn++;
        }
        if (j->remaining_time > 0) {
            j->status = JOB_READY;
            queue[qt] = i; qt = (qt + 1) % MAX_JOBS; qn++;
        } else {
            j->completion_time = t;
            done++;
        }
    }
    finalize(out);
    return 0;
}

/* ---------- Non-preemptive Priority ---------- */

static int better_priority(const Job *a, const Job *b)
{
    if (a->priority != b->priority) return a->priority < b->priority;      /* smaller = higher */
    if (a->arrival_time != b->arrival_time) return a->arrival_time < b->arrival_time;
    return a->id < b->id;
}

int sched_priority(const JobSet *in, SchedResult *out)
{
    if (in->count <= 0) return -1;
    init_result(in, out, "Priority (non-preemptive)",
                "Smaller number = higher priority; tie-break: earlier arrival, then smaller job ID");
    int n = out->count, done = 0, t = 0;
    int finished[MAX_JOBS] = { 0 };

    while (done < n) {
        int best = -1;
        for (int i = 0; i < n; i++) {
            if (finished[i] || out->jobs[i].arrival_time > t) continue;
            if (best < 0 || better_priority(&out->jobs[i], &out->jobs[best])) best = i;
        }
        if (best < 0) {                              /* idle until the earliest unfinished arrival */
            int na = INT_MAX;
            for (int i = 0; i < n; i++)
                if (!finished[i] && out->jobs[i].arrival_time < na) na = out->jobs[i].arrival_time;
            add_segment(out, 0, t, na);
            t = na;
            continue;
        }
        Job *j = &out->jobs[best];
        j->start_time = t;
        add_segment(out, j->id, t, t + j->burst_time);
        t += j->burst_time;
        j->completion_time = t;
        finished[best] = 1;
        done++;
    }
    finalize(out);
    return 0;
}

/* ---------- printing ---------- */

void sched_print(const SchedResult *r)
{
    printf("\n==== %s ====\n", r->name);
    printf("Rule: %s\n", r->rule);
    if (r->quantum > 0) printf("Time quantum: %d\n", r->quantum);
    printf("Tie-breaking: earlier arrival time first, then smaller job ID\n");

    printf("\nExecution order / timeline  [start JOB end]:\n  ");
    for (int s = 0; s < r->nseg; s++) {
        const Segment *g = &r->timeline[s];
        if (g->job_id == 0) printf("[%d IDLE %d]", g->start, g->end);
        else                printf("[%d P%d %d]", g->start, g->job_id, g->end);
        if (s + 1 < r->nseg) printf(" -> ");
        if ((s + 1) % 5 == 0 && s + 1 < r->nseg) printf("\n  ");
    }
    printf("\n\n%-5s %-8s %-6s %-5s %-6s %-5s %-5s %-5s %-10s\n",
           "Job", "Arrival", "Burst", "Prio", "Start", "CT", "TAT", "WT", "Status");
    printf("-------------------------------------------------------------------\n");
    for (int i = 0; i < r->count; i++) {
        const Job *j = &r->jobs[i];
        printf("P%-4d %-8d %-6d %-5d %-6d %-5d %-5d %-5d %-10s\n", j->id, j->arrival_time,
               j->burst_time, j->priority, j->start_time, j->completion_time,
               j->turnaround_time, j->waiting_time, job_status_name(j->status));
    }
    printf("-------------------------------------------------------------------\n");
    printf("Average turnaround time = %.2f\nAverage waiting time    = %.2f\n", r->avg_tat, r->avg_wt);
    printf("(CT = completion time, TAT = CT - arrival, WT = TAT - burst)\n");
}

/* ---------- menu / CLI entry points ---------- */

static int require_jobs(void)
{
    if (g_jobs.count == 0) {
        printf("Error: no jobs loaded. Use 'Manage jobs' (menu option 1) to add jobs or load the sample set.\n");
        return -1;
    }
    return 0;
}

static void log_completions(const SchedResult *r)
{
    log_event("INFO", "%s started with %d jobs", r->name, r->count);
    for (int s = 0; s < r->nseg; s++)
        if (r->timeline[s].job_id != 0)
            log_event("INFO", "Job %d scheduled at t=%d", r->timeline[s].job_id, r->timeline[s].start);
    for (int i = 0; i < r->count; i++)
        log_event("INFO", "Job %d completed at t=%d", r->jobs[i].id, r->jobs[i].completion_time);
}

static int get_quantum(int quantum)
{
    if (quantum > 0) return quantum;
    if (read_int_range("Enter time quantum (1-20): ", 1, 20, &quantum) < 0) return -1;
    return quantum;
}

int run_fcfs(void)
{
    static SchedResult r;
    if (require_jobs() != 0) return -1;
    if (sched_fcfs(&g_jobs, &r) != 0) return -1;
    sched_print(&r);
    log_completions(&r);
    return 0;
}

int run_rr(int quantum)
{
    static SchedResult r;
    if (require_jobs() != 0) return -1;
    quantum = get_quantum(quantum);
    if (quantum <= 0 || sched_rr(&g_jobs, quantum, &r) != 0) return -1;
    sched_print(&r);
    log_completions(&r);
    return 0;
}

int run_priority(void)
{
    static SchedResult r;
    if (require_jobs() != 0) return -1;
    if (sched_priority(&g_jobs, &r) != 0) return -1;
    sched_print(&r);
    log_completions(&r);
    return 0;
}

int run_compare(int quantum)
{
    static SchedResult res[3];
    if (require_jobs() != 0) return -1;
    quantum = get_quantum(quantum);
    if (quantum <= 0) return -1;
    if (sched_fcfs(&g_jobs, &res[0]) != 0 || sched_rr(&g_jobs, quantum, &res[1]) != 0 ||
        sched_priority(&g_jobs, &res[2]) != 0) return -1;

    printf("\nJob set used for all three algorithms:");
    job_print(&g_jobs);
    for (int i = 0; i < 3; i++) { sched_print(&res[i]); log_completions(&res[i]); }

    printf("\n==== COMPARISON (same job set, RR quantum = %d) ====\n", quantum);
    printf("%-28s %-12s %-12s %-10s\n", "Algorithm", "Avg TAT", "Avg WT", "Slices");
    printf("-----------------------------------------------------------------\n");
    int best_wt = 0, best_tat = 0;
    for (int i = 0; i < 3; i++) {
        printf("%-28s %-12.2f %-12.2f %-10d\n", res[i].name, res[i].avg_tat, res[i].avg_wt, res[i].nseg);
        if (res[i].avg_wt < res[best_wt].avg_wt) best_wt = i;
        if (res[i].avg_tat < res[best_tat].avg_tat) best_tat = i;
    }
    printf("-----------------------------------------------------------------\n");
    printf("Lowest average waiting time   : %s\n", res[best_wt].name);
    printf("Lowest average turnaround time: %s\n", res[best_tat].name);
    log_event("INFO", "Scheduler comparison done: best WT=%s, best TAT=%s",
              res[best_wt].name, res[best_tat].name);
    return 0;
}
