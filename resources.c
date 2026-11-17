#include "resources.h"
#include "ipc.h"
#include "logger.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void print_limit(const char *label, const struct rlimit *l)
{
    printf("  %s: soft=", label);
    if (l->rlim_cur == RLIM_INFINITY) printf("unlimited"); else printf("%llu", (unsigned long long)l->rlim_cur);
    printf(" hard=");
    if (l->rlim_max == RLIM_INFINITY) printf("unlimited\n"); else printf("%llu\n", (unsigned long long)l->rlim_max);
}

/* Part 1: lower RLIMIT_NOFILE in this process, open files until open() fails, then restore. */
static int demo_nofile(void)
{
    struct rlimit old, lim;
    int fds[64], n = 0, rc = 0;

    printf("--- Limit 1: RLIMIT_NOFILE (max open file descriptors) ---\n");
    if (getrlimit(RLIMIT_NOFILE, &old) != 0) { perror("getrlimit"); return -1; }
    print_limit("before", &old);

    lim = old;
    lim.rlim_cur = 32;
    if (old.rlim_max != RLIM_INFINITY && lim.rlim_cur > old.rlim_max) lim.rlim_cur = old.rlim_max;
    if (setrlimit(RLIMIT_NOFILE, &lim) != 0) { perror("setrlimit"); return -1; }
    struct rlimit now;
    getrlimit(RLIMIT_NOFILE, &now);
    print_limit("applied", &now);

    printf("Opening /dev/null repeatedly (up to 64 times)...\n");
    int err = 0;
    for (int i = 0; i < 64; i++) {
        int fd = open("/dev/null", O_RDONLY);
        if (fd < 0) { err = errno; break; }
        fds[n++] = fd;
    }
    if (err != 0) {
        printf("Observed effect: open() failed after %d extra descriptors: errno=%d (%s)\n", n, err, strerror(err));
        printf("  (stdin, stdout, stderr and the log file already used some of the %llu allowed)\n",
               (unsigned long long)now.rlim_cur);
        if (err != EMFILE) rc = -1;
    } else {
        printf("No failure observed (limit not effective?)\n");
        rc = -1;
    }
    for (int i = 0; i < n; i++) close(fds[i]);
    printf("Closed all %d test descriptors.\n", n);

    if (setrlimit(RLIMIT_NOFILE, &old) != 0) { perror("setrlimit(restore)"); rc = -1; }
    getrlimit(RLIMIT_NOFILE, &now);
    print_limit("restored", &now);
    log_event(rc == 0 ? "INFO" : "ERROR", "RLIMIT_NOFILE demo: open() failed after %d fds", n);
    return rc;
}

/* Part 2: a child limits its own CPU time to 1 s and spins; the kernel sends SIGXCPU. */
static int demo_cpu(void)
{
    printf("\n--- Limit 2: RLIMIT_CPU (CPU seconds), applied inside a child process ---\n");
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return -1; }
    if (pid == 0) {
        struct rlimit core = { 0, 0 };
        setrlimit(RLIMIT_CORE, &core);                /* never leave a core file behind */
        struct rlimit lim = { 1, 2 };                 /* soft 1 s -> SIGXCPU, hard 2 s -> SIGKILL */
        if (setrlimit(RLIMIT_CPU, &lim) != 0) { perror("setrlimit(CPU)"); _exit(2); }
        printf("  [child pid=%d] RLIMIT_CPU set to soft=1s hard=2s, now busy-looping...\n", (int)getpid());
        fflush(stdout);
        alarm(10);                                    /* safety net: the demo can never hang */
        volatile unsigned long x = 0;
        for (;;) x++;
    }
    int st = 0;
    pid_t w;
    while ((w = waitpid(pid, &st, 0)) == -1 && errno == EINTR) { }
    if (w < 0) { perror("waitpid"); return -1; }
    int rc = -1;
    if (WIFSIGNALED(st) && WTERMSIG(st) == SIGXCPU) {
        printf("Observed effect: child pid %d was killed by SIGXCPU (%s) after using 1 s of CPU time.\n",
               (int)pid, strsignal(SIGXCPU));
        rc = 0;
    } else if (WIFSIGNALED(st)) {
        printf("Child ended by signal %d (%s)\n", WTERMSIG(st), strsignal(WTERMSIG(st)));
    } else {
        printf("Child ended unexpectedly (raw status %d)\n", st);
    }
    log_event(rc == 0 ? "INFO" : "ERROR", "RLIMIT_CPU demo: child pid %d %s", (int)pid,
              rc == 0 ? "stopped by SIGXCPU" : "did not behave as expected");
    return rc;
}

int run_rlimit_demo(void)
{
    printf("\n=== Resource Limit Demonstration (setrlimit) ===\n");
    int a = demo_nofile();
    int b = demo_cpu();
    return (a == 0 && b == 0) ? 0 : -1;
}

static int count_threads(void)
{
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[128];
    int n = -1;
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "Threads: %d", &n) == 1) break;
    fclose(f);
    return n;
}

int run_cleanup_check(void)
{
    int rc = 0;
    printf("\n=== Cleanup Verification ===\n");

    pid_t r = waitpid(-1, NULL, WNOHANG);
    if (r == -1 && errno == ECHILD) printf("Child processes : none remaining (all reaped, no zombies)\n");
    else { printf("Child processes : a stray child (pid %d) was found and reaped\n", (int)r); rc = -1; }

    int th = count_threads();
    int expected = logger_is_running() ? 2 : 1;
    printf("Threads         : %d in this process (expected %d: main%s)\n", th, expected,
           logger_is_running() ? " + logger consumer" : "");
    if (th != expected) rc = -1;

    printf("IPC objects     :\n");
    if (ipc_verify_cleanup(1) != 0) rc = -1;
    printf("Result: %s\n", rc == 0 ? "everything created by the program has been released"
                                   : "something was left behind");
    log_event(rc == 0 ? "INFO" : "ERROR", "Cleanup verification: %s", rc == 0 ? "clean" : "leftovers found");
    return rc;
}
