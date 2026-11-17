#include "job.h"
#include "logger.h"
#include "util.h"
#include <stdio.h>
#include <string.h>

JobSet g_jobs = { .count = 0 };

const char *job_status_name(JobStatus s)
{
    switch (s) {
    case JOB_NEW:       return "NEW";
    case JOB_READY:     return "READY";
    case JOB_RUNNING:   return "RUNNING";
    case JOB_COMPLETED: return "COMPLETED";
    }
    return "UNKNOWN";
}

/* Check every field; never silently "fixes" a value. */
int job_validate(const JobSet *set, int id, int arrival, int burst, int priority,
                 char *err, size_t errsz)
{
    if (set->count >= MAX_JOBS) {
        snprintf(err, errsz, "job table is full (maximum %d jobs)", MAX_JOBS);
        return -1;
    }
    if (id < 1 || id > MAX_ID) {
        snprintf(err, errsz, "job ID must be between 1 and %d", MAX_ID);
        return -1;
    }
    for (int i = 0; i < set->count; i++) {
        if (set->jobs[i].id == id) {
            snprintf(err, errsz, "job ID %d already exists", id);
            return -1;
        }
    }
    if (arrival < 0 || arrival > MAX_ARRIVAL) {
        snprintf(err, errsz, "arrival time must be between 0 and %d", MAX_ARRIVAL);
        return -1;
    }
    if (burst < 1 || burst > MAX_BURST) {
        snprintf(err, errsz, "CPU burst must be between 1 and %d", MAX_BURST);
        return -1;
    }
    if (priority < MIN_PRIORITY || priority > MAX_PRIORITY) {
        snprintf(err, errsz, "priority must be between %d and %d (1 = highest)",
                 MIN_PRIORITY, MAX_PRIORITY);
        return -1;
    }
    return 0;
}

static void store_job(JobSet *set, int id, int arrival, int burst, int priority)
{
    Job *j = &set->jobs[set->count++];
    memset(j, 0, sizeof *j);
    j->id = id;
    j->arrival_time = arrival;
    j->burst_time = burst;
    j->priority = priority;
    j->status = JOB_NEW;
    j->remaining_time = burst;
    j->start_time = -1;
}

int job_add(JobSet *set, int id, int arrival, int burst, int priority)
{
    char err[128];
    if (job_validate(set, id, arrival, burst, priority, err, sizeof err) != 0) {
        printf("Error: %s.\n", err);
        log_event("WARN", "Job rejected: %s", err);
        return -1;
    }
    store_job(set, id, arrival, burst, priority);
    log_event("INFO", "Job %d created (arrival=%d burst=%d priority=%d)",
              id, arrival, burst, priority);
    return 0;
}

/* Fixed, repeatable test set: has an idle gap (P5) and a priority tie (P2 vs P4). */
void job_load_sample(JobSet *set)
{
    static const int data[][4] = {
        /* id arrival burst priority */
        { 1,  0, 5, 3 },
        { 2,  1, 3, 1 },
        { 3,  2, 8, 4 },
        { 4,  3, 6, 1 },
        { 5, 25, 2, 2 },
    };
    set->count = 0;
    for (size_t i = 0; i < sizeof data / sizeof data[0]; i++)
        store_job(set, data[i][0], data[i][1], data[i][2], data[i][3]);
}

void job_print(const JobSet *set)
{
    if (set->count == 0) { printf("No jobs stored.\n"); return; }
    printf("\n%-5s %-8s %-6s %-9s %-10s\n", "Job", "Arrival", "Burst", "Priority", "Status");
    printf("--------------------------------------------\n");
    for (int i = 0; i < set->count; i++) {
        const Job *j = &set->jobs[i];
        printf("P%-4d %-8d %-6d %-9d %-10s\n", j->id, j->arrival_time, j->burst_time,
               j->priority, job_status_name(j->status));
    }
    printf("(priority: smaller number = higher priority)\n");
}

int jobs_menu(void)
{
    for (;;) {
        int c;
        printf("\n--- Job management ---\n"
               " 1. Add a job\n 2. Load sample job set\n 3. Show jobs\n"
               " 4. Clear all jobs\n 0. Back\n");
        if (read_int_range("Choice: ", 0, 4, &c) < 0) return 0;
        if (c == 0) return 0;
        if (c == 1) {
            int id, a, b, p;
            if (g_jobs.count >= MAX_JOBS) { printf("Error: job table is full.\n"); continue; }
            if (read_int_range("Job ID (1-9999): ", 1, MAX_ID, &id) < 0) return 0;
            if (read_int_range("Arrival time (0-1000): ", 0, MAX_ARRIVAL, &a) < 0) return 0;
            if (read_int_range("CPU burst (1-100): ", 1, MAX_BURST, &b) < 0) return 0;
            if (read_int_range("Priority (1-10, 1=highest): ", MIN_PRIORITY, MAX_PRIORITY, &p) < 0) return 0;
            if (job_add(&g_jobs, id, a, b, p) == 0) printf("Job P%d added.\n", id);
        } else if (c == 2) {
            job_load_sample(&g_jobs);
            log_event("INFO", "Sample job set loaded (%d jobs)", g_jobs.count);
            printf("Sample job set loaded.\n");
            job_print(&g_jobs);
        } else if (c == 3) {
            job_print(&g_jobs);
        } else if (c == 4) {
            g_jobs.count = 0;
            log_event("INFO", "All jobs cleared");
            printf("All jobs cleared.\n");
        }
    }
}
