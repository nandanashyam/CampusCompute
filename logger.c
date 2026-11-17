#include "logger.h"
#include "util.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define QUEUE_CAP 8

typedef struct {
    unsigned seq;
    time_t ts;
    char level[8];
    char thread[16];
    char msg[160];
} LogEvent;

static LogEvent queue_buf[QUEUE_CAP];
static int q_head, q_tail, q_count;
static pthread_mutex_t mtx;
static pthread_cond_t not_full, not_empty;
static pthread_t consumer;
static FILE *logf;
static int running, shutting_down;
static int echo_on, delay_ms;
static unsigned next_seq, produced, consumed;
static char log_path[256] = "campuscompute.log";
static _Thread_local char t_name[16] = "main";

static void copy_str(char *dst, size_t dstsz, const char *src)
{
    size_t n = strnlen(src, dstsz - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void format_time(time_t t, char *out, size_t sz)
{
    struct tm tmv;
    localtime_r(&t, &tmv);
    strftime(out, sz, "%Y-%m-%d %H:%M:%S", &tmv);
}

/* ---------- consumer thread ---------- */

static void *consumer_main(void *arg)
{
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&mtx);
        while (q_count == 0 && !shutting_down)
            pthread_cond_wait(&not_empty, &mtx);          /* queue EMPTY: wait */
        if (q_count == 0 && shutting_down) {              /* nothing left to drain: exit */
            pthread_mutex_unlock(&mtx);
            break;
        }
        LogEvent ev = queue_buf[q_head];
        q_head = (q_head + 1) % QUEUE_CAP;
        q_count--;
        int echo = echo_on, dly = delay_ms;
        pthread_cond_signal(&not_full);                   /* a slot was freed */
        pthread_mutex_unlock(&mtx);

        char tb[32];
        format_time(ev.ts, tb, sizeof tb);
        fprintf(logf, "%s [%-5s] #%03u (%s) %s\n", tb, ev.level, ev.seq, ev.thread, ev.msg);
        fflush(logf);

        pthread_mutex_lock(&mtx);
        consumed++;
        pthread_mutex_unlock(&mtx);
        if (echo) printf("  [CONSUMER] wrote event #%u from %s\n", ev.seq, ev.thread);
        if (dly > 0) sleep_ms(dly);
    }
    return NULL;
}

/* ---------- public API ---------- */

int logger_start(const char *path)
{
    if (running) return 0;
    if (path) copy_str(log_path, sizeof log_path, path);
    logf = fopen(log_path, "a");
    if (!logf) { perror("logger: cannot open log file"); return -1; }
    pthread_mutex_init(&mtx, NULL);
    pthread_cond_init(&not_full, NULL);
    pthread_cond_init(&not_empty, NULL);
    q_head = q_tail = q_count = 0;
    shutting_down = 0;
    next_seq = produced = consumed = 0;

    char tb[32];
    format_time(time(NULL), tb, sizeof tb);
    fprintf(logf, "%s ==== logger session started ====\n", tb);
    fflush(logf);

    running = 1;
    int rc = pthread_create(&consumer, NULL, consumer_main, NULL);
    if (rc != 0) {
        fprintf(stderr, "logger: pthread_create failed: %s\n", strerror(rc));
        running = 0;
        pthread_mutex_destroy(&mtx);
        pthread_cond_destroy(&not_full);
        pthread_cond_destroy(&not_empty);
        fclose(logf); logf = NULL;
        return -1;
    }
    return 0;
}

int logger_stop(void)
{
    if (!running) return 0;
    pthread_mutex_lock(&mtx);
    shutting_down = 1;                       /* consumer drains remaining events, then exits */
    pthread_cond_broadcast(&not_empty);
    pthread_cond_broadcast(&not_full);
    pthread_mutex_unlock(&mtx);

    int rc = pthread_join(consumer, NULL);
    if (rc != 0) fprintf(stderr, "logger: pthread_join failed: %s\n", strerror(rc));

    char tb[32];
    format_time(time(NULL), tb, sizeof tb);
    fprintf(logf, "%s [INFO ] Logger shutdown complete (produced=%u consumed=%u)\n", tb, produced, consumed);
    fclose(logf);
    logf = NULL;
    pthread_mutex_destroy(&mtx);
    pthread_cond_destroy(&not_full);
    pthread_cond_destroy(&not_empty);
    running = 0;
    return rc == 0 ? 0 : -1;
}

int logger_is_running(void) { return running; }
const char *logger_path(void) { return log_path; }

int log_event(const char *level, const char *fmt, ...)
{
    if (!running) return -1;
    LogEvent ev;
    memset(&ev, 0, sizeof ev);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ev.msg, sizeof ev.msg, fmt, ap);
    va_end(ap);
    copy_str(ev.level, sizeof ev.level, level);
    copy_str(ev.thread, sizeof ev.thread, t_name);
    ev.ts = time(NULL);

    pthread_mutex_lock(&mtx);
    int announced = 0;
    while (q_count == QUEUE_CAP && !shutting_down) {       /* queue FULL: wait */
        if (echo_on && !announced) {
            printf("  [%s] queue FULL (%d/%d) - waiting for consumer\n", t_name, q_count, QUEUE_CAP);
            announced = 1;
        }
        pthread_cond_wait(&not_full, &mtx);
    }
    if (shutting_down) { pthread_mutex_unlock(&mtx); return -1; }
    ev.seq = ++next_seq;
    queue_buf[q_tail] = ev;
    q_tail = (q_tail + 1) % QUEUE_CAP;
    q_count++;
    produced++;
    if (echo_on)
        printf("  [%s] queued event #%u (queue %d/%d)\n", t_name, ev.seq, q_count, QUEUE_CAP);
    pthread_cond_signal(&not_empty);
    pthread_mutex_unlock(&mtx);
    return 0;
}

void logger_flush(void)
{
    if (!running) return;
    for (int i = 0; i < 400; i++) {                        /* wait at most ~2 s */
        pthread_mutex_lock(&mtx);
        int done = (consumed == produced);
        pthread_mutex_unlock(&mtx);
        if (done) return;
        sleep_ms(5);
    }
}

void logger_set_echo(int on)
{
    if (running) { pthread_mutex_lock(&mtx); echo_on = on; pthread_mutex_unlock(&mtx); }
    else echo_on = on;
}

void logger_set_consumer_delay_ms(int ms)
{
    if (running) { pthread_mutex_lock(&mtx); delay_ms = ms; pthread_mutex_unlock(&mtx); }
    else delay_ms = ms;
}

void logger_set_thread_name(const char *name) { copy_str(t_name, sizeof t_name, name); }

void logger_stats(int *p, int *c)
{
    if (p) *p = (int)produced;
    if (c) *c = (int)consumed;
}

/* ---------- FR3 demonstration ---------- */

typedef struct { int id; int events; } ProducerArg;

static void *producer_main(void *arg)
{
    const ProducerArg *a = arg;
    char name[16];
    snprintf(name, sizeof name, "producer-%d", a->id);
    logger_set_thread_name(name);
    for (int i = 1; i <= a->events; i++)
        log_event("INFO", "Producer %d generated event %d", a->id, i);
    return NULL;
}

int run_logger_demo(void)
{
    enum { NPROD = 3, NEV = 5 };
    pthread_t tid[NPROD];
    ProducerArg args[NPROD];
    int created = 0, rc = 0;

    if (!running && logger_start(NULL) != 0) return -1;

    printf("\n=== Producer-Consumer Logger Demo ===\n");
    printf("Bounded queue capacity = %d, producers = %d (x%d events), consumer = 1 thread\n",
           QUEUE_CAP, NPROD, NEV);
    printf("Consumer is slowed (40 ms/event) so the queue fills and producers must wait.\n\n");
    logger_flush();                                        /* earlier events are not part of this test */
    int base_p, base_c;
    logger_stats(&base_p, &base_c);
    logger_set_echo(1);
    logger_set_consumer_delay_ms(40);

    for (int i = 0; i < NPROD; i++) {
        args[i].id = i + 1;
        args[i].events = NEV;
        int e = pthread_create(&tid[i], NULL, producer_main, &args[i]);
        if (e != 0) { fprintf(stderr, "pthread_create failed: %s\n", strerror(e)); rc = -1; break; }
        created++;
    }
    for (int i = 0; i < created; i++) pthread_join(tid[i], NULL);
    printf("\nAll %d producer threads joined.\n", created);

    printf("Requesting shutdown: consumer drains the queue, then exits...\n");
    int expected_total = created * NEV;
    if (logger_stop() != 0) rc = -1;                       /* joins the consumer thread */
    int p, c;
    logger_stats(&p, &c);
    p -= base_p;                                           /* events of this demo only */
    c -= base_c;
    printf("Consumer thread joined. Events produced = %d, consumed (written to file) = %d, expected = %d\n",
           p, c, expected_total);
    if (p != expected_total || c != expected_total) rc = -1;
    printf("Result: %s\n", rc == 0 ? "no events lost, threads terminated cleanly" : "MISMATCH detected");

    logger_set_echo(0);
    logger_set_consumer_delay_ms(0);
    if (logger_start(NULL) != 0) rc = -1;                  /* resume normal logging */
    log_event("INFO", "Logger demo finished: produced=%d consumed=%d", p, c);
    return rc;
}

int run_view_logs(void)
{
    logger_flush();
    FILE *f = fopen(log_path, "r");
    if (!f) { printf("Error: cannot open log file '%s' (no events logged yet?)\n", log_path); return -1; }
    char line[512];
    int total = 0;
    while (fgets(line, sizeof line, f)) total++;
    rewind(f);
    int skip = total > 40 ? total - 40 : 0, n = 0;
    printf("\n=== Log file: %s (%d lines total%s) ===\n", log_path, total,
           skip ? ", showing last 40" : "");
    while (fgets(line, sizeof line, f)) if (n++ >= skip) fputs(line, stdout);
    fclose(f);
    return 0;
}
