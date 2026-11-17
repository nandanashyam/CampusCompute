#include "worker.h"
#include "logger.h"
#include "util.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define SIM_EXIT_CODE 3

typedef enum { W_NORMAL = 1, W_FAIL_EXIT = 2, W_FAIL_SIGNAL = 3 } WorkerKind;

static const char *kind_name(WorkerKind k)
{
    switch (k) {
    case W_NORMAL:      return "normal worker";
    case W_FAIL_EXIT:   return "SIMULATED failure (non-zero exit)";
    case W_FAIL_SIGNAL: return "SIMULATED crash (self-raised signal)";
    }
    return "?";
}

/* Child body. Uses only printf/_exit: it must NOT touch the logger (the logger's thread does not
 * exist in the child after fork()). */
static void worker_body(WorkerKind k, int id)
{
    printf("  [worker %d pid=%d] started: %s\n", id, (int)getpid(), kind_name(k));
    if (k == W_NORMAL) {
        unsigned long sum = 0;
        for (unsigned long i = 1; i <= 1000000UL; i++) sum += i;
        printf("  [worker %d pid=%d] task done, sum(1..1000000) = %lu, exiting with status 0\n",
               id, (int)getpid(), sum);
        fflush(stdout);
        _exit(0);
    }
    if (k == W_FAIL_EXIT) {
        printf("  [worker %d pid=%d] SIMULATED application failure: exiting with status %d\n",
               id, (int)getpid(), SIM_EXIT_CODE);
        fflush(stdout);
        _exit(SIM_EXIT_CODE);
    }
    printf("  [worker %d pid=%d] SIMULATED crash: raising SIGTERM on itself\n", id, (int)getpid());
    fflush(stdout);
    raise(SIGTERM);
    _exit(99);          /* only reached if the signal was somehow ignored */
}

/* Parent side: fork one worker, waitpid() for it, classify the status. Returns 0 if as expected. */
static int run_one(WorkerKind k, int id)
{
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return -1; }
    if (pid == 0) worker_body(k, id);                 /* never returns */

    printf("Parent: created worker %d with pid %d (%s)\n", id, (int)pid, kind_name(k));
    int st = 0;
    pid_t w;
    while ((w = waitpid(pid, &st, 0)) == -1 && errno == EINTR) { }
    if (w < 0) { perror("waitpid"); return -1; }

    int expected = 0;
    if (WIFEXITED(st)) {
        int code = WEXITSTATUS(st);
        if (code == 0) {
            printf("Parent: worker pid %d COMPLETED NORMALLY (exit status 0)\n", (int)pid);
            log_event("INFO", "Worker pid %d completed normally", (int)pid);
        } else {
            printf("Parent: worker pid %d exited with NON-ZERO status %d -> failure detected\n"
                   "        (this is the SIMULATED failure; a non-zero exit code is not necessarily a crash)\n",
                   (int)pid, code);
            log_event("WARN", "Worker pid %d failed with exit status %d (simulated)", (int)pid, code);
        }
        expected = (k == W_NORMAL && code == 0) || (k == W_FAIL_EXIT && code == SIM_EXIT_CODE);
    } else if (WIFSIGNALED(st)) {
        int sig = WTERMSIG(st);
        printf("Parent: worker pid %d was TERMINATED BY SIGNAL %d (%s) -> abnormal termination detected\n"
               "        (the worker raised this signal itself: SIMULATED crash, not a real OS failure)\n",
               (int)pid, sig, strsignal(sig));
        log_event("WARN", "Worker pid %d killed by signal %d (simulated)", (int)pid, sig);
        expected = (k == W_FAIL_SIGNAL && sig == SIGTERM);
    } else {
        printf("Parent: worker pid %d changed state in an unexpected way (raw status %d)\n", (int)pid, st);
    }
    printf("Parent: detection %s\n\n", expected ? "matches what the worker was programmed to do" : "DOES NOT MATCH");
    return expected ? 0 : -1;
}

int run_worker_demo(int mode)
{
    if (mode == 0) {
        printf("\n--- Worker processes ---\n 1. Normal worker\n 2. Simulated failure (non-zero exit)\n"
               " 3. Simulated crash (signal)\n 4. All three\n 0. Back\n");
        if (read_int_range("Choice: ", 0, 4, &mode) < 0 || mode == 0) return -1;
    }
    if (mode < 1 || mode > 4) return -1;
    printf("\n=== Worker Process Monitoring (fork + waitpid) ===\n");
    printf("NOTE: failures below are SIMULATED by the workers themselves, not real OS failures.\n\n");

    int rc = 0, id = 1;
    if (mode == 1 || mode == 4) if (run_one(W_NORMAL, id++) != 0) rc = -1;
    if (mode == 2 || mode == 4) if (run_one(W_FAIL_EXIT, id++) != 0) rc = -1;
    if (mode == 3 || mode == 4) if (run_one(W_FAIL_SIGNAL, id++) != 0) rc = -1;

    /* No children should remain: waitpid(-1) must fail with ECHILD (no zombies left). */
    pid_t left = waitpid(-1, NULL, WNOHANG);
    if (left == -1 && errno == ECHILD) printf("All workers reaped: no zombie or running children remain.\n");
    else { printf("Warning: an unexpected child (pid %d) was still present.\n", (int)left); rc = -1; }
    return rc;
}
