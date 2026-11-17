#include "ipc.h"
#include "job.h"
#include "logger.h"
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* Names created (and later unlinked) ONLY by this program. */
#define SHM_NAME       "/campuscompute_shm"
#define SEM_EMPTY_NAME "/campuscompute_sem_empty"   /* sender waits on it: slot is free   */
#define SEM_FULL_NAME  "/campuscompute_sem_full"    /* receiver waits on it: slot is full */

/* One-slot mailbox in shared memory (classic producer/consumer with two semaphores). */
typedef struct {
    int seq;
    int last;                 /* 1 on the final message */
    unsigned checksum;        /* computed by sender over text, re-computed by receiver */
    char text[200];
} ShmMsg;

static unsigned checksum_of(const char *s)
{
    unsigned c = 0;
    while (*s) c = c * 31u + (unsigned char)*s++;
    return c;
}

/* sem_wait with a timeout so a dead peer can never hang the demo. */
static int sem_wait_timeout(sem_t *s, int secs)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += secs;
    int r;
    while ((r = sem_timedwait(s, &ts)) == -1 && errno == EINTR) { }
    return r;
}

/* ---------- receiver: runs in the CHILD process ---------- */

static void receiver_main(ShmMsg *inh_shm, int inh_fd, sem_t *inh_empty, sem_t *inh_full)
{
    /* Release the handles inherited through fork(); open everything again BY NAME. */
    munmap(inh_shm, sizeof(ShmMsg));
    close(inh_fd);
    sem_close(inh_empty);
    sem_close(inh_full);

    int status = 1;
    int fd = shm_open(SHM_NAME, O_RDWR, 0);
    if (fd < 0) { perror("[RECEIVER] shm_open"); _exit(1); }
    ShmMsg *shm = mmap(NULL, sizeof(ShmMsg), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) { perror("[RECEIVER] mmap"); close(fd); _exit(1); }
    sem_t *empty = sem_open(SEM_EMPTY_NAME, 0);
    sem_t *full = sem_open(SEM_FULL_NAME, 0);
    if (empty == SEM_FAILED || full == SEM_FAILED) {
        perror("[RECEIVER] sem_open");
        munmap(shm, sizeof(ShmMsg)); close(fd);
        _exit(1);
    }

    int received = 0, bad = 0;
    for (;;) {
        if (sem_wait_timeout(full, 5) != 0) { perror("[RECEIVER] sem_wait"); bad++; break; }
        ShmMsg m = *shm;                                    /* copy out of shared memory */
        int ok = (checksum_of(m.text) == m.checksum);
        printf("  [RECEIVER pid=%d] read  msg #%d: \"%s\"  checksum %s\n",
               (int)getpid(), m.seq, m.text, ok ? "OK" : "MISMATCH");
        if (!ok) bad++;
        received++;
        sem_post(empty);                                    /* slot is free again */
        if (m.last) break;
    }
    printf("  [RECEIVER pid=%d] finished: %d messages received, %d bad\n", (int)getpid(), received, bad);
    fflush(stdout);
    status = (bad == 0 && received > 0) ? 0 : 1;

    munmap(shm, sizeof(ShmMsg));
    close(fd);
    sem_close(empty);
    sem_close(full);
    _exit(status);                                          /* never return into the menu */
}

/* ---------- demo: runs in the PARENT (sender) ---------- */

int run_ipc_demo(void)
{
    int rc = -1;
    int fd = -1;
    ShmMsg *shm = MAP_FAILED;
    sem_t *empty = SEM_FAILED, *full = SEM_FAILED;
    int shm_created = 0, empty_created = 0, full_created = 0;
    pid_t child = -1;

    printf("\n=== Shared-Memory IPC Demo (sender = parent, receiver = child) ===\n");

    /* Remove stale objects left by a crashed earlier run of THIS program (same names only). */
    shm_unlink(SHM_NAME);
    sem_unlink(SEM_EMPTY_NAME);
    sem_unlink(SEM_FULL_NAME);

    fd = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) { perror("shm_open"); goto cleanup; }
    shm_created = 1;
    if (ftruncate(fd, sizeof(ShmMsg)) != 0) { perror("ftruncate"); goto cleanup; }
    shm = mmap(NULL, sizeof(ShmMsg), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) { perror("mmap"); goto cleanup; }
    memset(shm, 0, sizeof *shm);

    empty = sem_open(SEM_EMPTY_NAME, O_CREAT | O_EXCL, 0600, 1);   /* slot initially free */
    if (empty == SEM_FAILED) { perror("sem_open(empty)"); goto cleanup; }
    empty_created = 1;
    full = sem_open(SEM_FULL_NAME, O_CREAT | O_EXCL, 0600, 0);     /* nothing to read yet */
    if (full == SEM_FAILED) { perror("sem_open(full)"); goto cleanup; }
    full_created = 1;
    printf("Created: shared memory %s (%zu bytes), semaphores %s (init 1) and %s (init 0)\n",
           SHM_NAME, sizeof(ShmMsg), SEM_EMPTY_NAME, SEM_FULL_NAME);

    /* Messages to send: the job records (sample set if none loaded). */
    JobSet tmp;
    if (g_jobs.count > 0) tmp = g_jobs; else job_load_sample(&tmp);
    int total = tmp.count + 1;

    fflush(stdout);
    child = fork();
    if (child < 0) { perror("fork"); goto cleanup; }
    if (child == 0) receiver_main(shm, fd, empty, full);   /* does not return */

    printf("Sender pid=%d started receiver pid=%d\n", (int)getpid(), (int)child);
    int send_ok = 1;
    for (int i = 0; i < total; i++) {
        if (sem_wait_timeout(empty, 5) != 0) { perror("[SENDER] sem_wait"); send_ok = 0; break; }
        shm->seq = i + 1;
        shm->last = (i == total - 1);
        if (i < tmp.count) {
            const Job *j = &tmp.jobs[i];
            snprintf(shm->text, sizeof shm->text, "Job P%d: arrival=%d burst=%d priority=%d",
                     j->id, j->arrival_time, j->burst_time, j->priority);
        } else {
            snprintf(shm->text, sizeof shm->text, "END-OF-TRANSMISSION after %d job records", tmp.count);
        }
        shm->checksum = checksum_of(shm->text);
        printf("  [SENDER   pid=%d] wrote msg #%d: \"%s\"\n", (int)getpid(), shm->seq, shm->text);
        fflush(stdout);
        sem_post(full);                                    /* tell receiver data is complete */
    }

    int st = 0;
    pid_t w;
    while ((w = waitpid(child, &st, 0)) == -1 && errno == EINTR) { }
    child = -1;
    if (w < 0) { perror("waitpid"); goto cleanup; }
    if (WIFEXITED(st) && WEXITSTATUS(st) == 0 && send_ok) {
        printf("Receiver exited normally (status 0): all data matched what the sender wrote.\n");
        rc = 0;
    } else {
        printf("Receiver reported a problem (raw status %d).\n", st);
    }

cleanup:
    if (child > 0) { kill(child, SIGTERM); waitpid(child, NULL, 0); }
    if (shm != MAP_FAILED) munmap(shm, sizeof(ShmMsg));
    if (fd >= 0) close(fd);
    if (empty != SEM_FAILED) sem_close(empty);
    if (full != SEM_FAILED) sem_close(full);
    if (empty_created) sem_unlink(SEM_EMPTY_NAME);
    if (full_created) sem_unlink(SEM_FULL_NAME);
    if (shm_created) shm_unlink(SHM_NAME);
    printf("Cleanup: unmapped memory, closed handles, unlinked %s, %s, %s\n",
           SHM_NAME, SEM_EMPTY_NAME, SEM_FULL_NAME);
    if (ipc_verify_cleanup(1) != 0) rc = -1;
    log_event(rc == 0 ? "INFO" : "ERROR", "IPC demo %s", rc == 0 ? "completed, resources released" : "failed");
    return rc;
}

/* Try to open each name WITHOUT O_CREAT: success means the object still exists (leak). */
int ipc_verify_cleanup(int verbose)
{
    int leaked = 0;
    int fd = shm_open(SHM_NAME, O_RDWR, 0);
    if (fd >= 0) { close(fd); leaked++; if (verbose) printf("  LEAK: shared memory %s still exists\n", SHM_NAME); }
    else if (verbose) printf("  OK: shared memory %s released (%s)\n", SHM_NAME, strerror(errno));

    const char *names[2] = { SEM_EMPTY_NAME, SEM_FULL_NAME };
    for (int i = 0; i < 2; i++) {
        sem_t *s = sem_open(names[i], 0);
        if (s != SEM_FAILED) { sem_close(s); leaked++; if (verbose) printf("  LEAK: semaphore %s still exists\n", names[i]); }
        else if (verbose) printf("  OK: semaphore %s released (%s)\n", names[i], strerror(errno));
    }
    return leaked == 0 ? 0 : -1;
}
