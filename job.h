#ifndef JOB_H
#define JOB_H
#include <stddef.h>

#define MAX_JOBS     20
#define MAX_ID       9999
#define MAX_ARRIVAL  1000
#define MAX_BURST    100
#define MIN_PRIORITY 1      /* 1 = highest priority */
#define MAX_PRIORITY 10

typedef enum { JOB_NEW, JOB_READY, JOB_RUNNING, JOB_COMPLETED } JobStatus;

typedef struct {
    int id;
    int arrival_time;
    int burst_time;
    int priority;
    JobStatus status;
    int remaining_time;
    int start_time;          /* first time on CPU, -1 if never ran */
    int completion_time;
    int turnaround_time;
    int waiting_time;
} Job;

typedef struct {
    Job jobs[MAX_JOBS];
    int count;
} JobSet;

extern JobSet g_jobs;        /* the job set used by the menu / CLI */

const char *job_status_name(JobStatus s);
int  job_validate(const JobSet *set, int id, int arrival, int burst, int priority,
                  char *err, size_t errsz);
int  job_add(JobSet *set, int id, int arrival, int burst, int priority); /* 0 ok, -1 rejected */
void job_load_sample(JobSet *set);
void job_print(const JobSet *set);
int  jobs_menu(void);
#endif
