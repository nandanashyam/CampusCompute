/* CampusCompute - OS Level 3 mini-project: scheduling, logging, IPC, Banker's, workers, limits. */
#include "banker.h"
#include "ipc.h"
#include "job.h"
#include "logger.h"
#include "resources.h"
#include "scheduler.h"
#include "util.h"
#include "worker.h"
#include <stdio.h>
#include <string.h>

#define LOG_FILE "campuscompute.log"

static void print_usage(const char *prog)
{
    printf("CampusCompute - job scheduling and resource management prototype\n\n"
           "Usage: %s                     interactive menu\n"
           "       %s <command> [option]  run one feature directly\n\n"
           "Commands:\n"
           "  help                 show this message\n"
           "  fcfs                 FCFS scheduling on the sample job set\n"
           "  rr [quantum]         Round Robin (quantum 1-20, default 2)\n"
           "  priority             non-preemptive Priority scheduling\n"
           "  compare [quantum]    compare all three algorithms on the same jobs\n"
           "  logger               producer-consumer logger demo (pthreads)\n"
           "  ipc                  shared-memory + named-semaphore IPC demo\n"
           "  banker [safe|unsafe|custom]   Banker's safety algorithm (default: safe then unsafe)\n"
           "  worker [normal|fail|crash|all] fork/waitpid worker monitoring (default: all)\n"
           "  rlimit               setrlimit demonstrations\n"
           "  logs                 show the tail of %s\n"
           "  cleanup              verify children, threads and IPC objects are released\n"
           "  all                  integrated demonstration with summary\n"
           "  menu                 interactive menu\n",
           prog, prog, LOG_FILE);
}

static void print_menu(void)
{
    printf("\n========== CAMPUSCOMPUTE ==========\n"
           " 1. Manage jobs (add / load sample / show / clear)\n"
           " 2. Run FCFS\n"
           " 3. Run Round Robin\n"
           " 4. Run Priority Scheduling\n"
           " 5. Compare Scheduling Algorithms\n"
           " 6. Run Concurrent Logger (producer-consumer)\n"
           " 7. Demonstrate Shared-Memory IPC\n"
           " 8. Run Banker's Algorithm\n"
           " 9. Test Worker Processes\n"
           "10. Demonstrate Resource Limits\n"
           "11. View Logs\n"
           "12. Verify Cleanup\n"
           "13. Run Integrated Demo\n"
           "14. Help / usage\n"
           " 0. Exit\n");
}

/* ---------- integrated demonstration ---------- */

typedef struct { const char *name; int pass; } Step;

static int run_all(void)
{
    Step steps[8];
    int n = 0;

    printf("\n############ INTEGRATED DEMONSTRATION ############\n");
    job_load_sample(&g_jobs);
    log_event("INFO", "Integrated demo started");
    steps[n].name = "CPU scheduling (FCFS, RR, Priority)";
    steps[n++].pass = (run_compare(2) == 0);

    steps[n].name = "Producer-consumer logger (pthreads)";
    steps[n++].pass = (run_logger_demo() == 0);

    steps[n].name = "Shared-memory IPC + semaphores";
    steps[n++].pass = (run_ipc_demo() == 0);

    steps[n].name = "Banker's algorithm: safe state detected";
    steps[n++].pass = (run_banker_demo(1) == 1);

    steps[n].name = "Banker's algorithm: unsafe state detected";
    steps[n++].pass = (run_banker_demo(2) == 0);

    steps[n].name = "Worker monitoring (normal + simulated failures)";
    steps[n++].pass = (run_worker_demo(4) == 0);

    steps[n].name = "Resource limits (setrlimit)";
    steps[n++].pass = (run_rlimit_demo() == 0);

    steps[n].name = "Cleanup verification";
    steps[n++].pass = (run_cleanup_check() == 0);

    printf("\n############ SUMMARY ############\n");
    int ok = 0;
    for (int i = 0; i < n; i++) {
        printf(" [%s] %s\n", steps[i].pass ? "PASS" : "FAIL", steps[i].name);
        ok += steps[i].pass;
    }
    printf("\n%d of %d modules passed. Log file: %s\n", ok, n, logger_path());
    log_event(ok == n ? "INFO" : "ERROR", "Integrated demo finished: %d/%d passed", ok, n);
    return ok == n ? 0 : 1;
}

/* ---------- interactive menu ---------- */

static int menu_loop(void)
{
    char buf[64];
    print_menu();
    for (;;) {
        int c;
        if (read_line("\nEnter choice: ", buf, sizeof buf) < 0) { printf("\n"); break; }
        if (parse_int(buf, 0, 14, &c) != 0) {
            printf("Error: invalid choice '%s'. Enter a number from 0 to 14 (14 = help).\n", buf);
            log_event("WARN", "Invalid menu choice: '%s'", buf);
            continue;
        }
        switch (c) {
        case 0:  return 0;
        case 1:  jobs_menu(); break;
        case 2:  run_fcfs(); break;
        case 3:  run_rr(0); break;
        case 4:  run_priority(); break;
        case 5:  run_compare(0); break;
        case 6:  run_logger_demo(); break;
        case 7:  run_ipc_demo(); break;
        case 8:  run_banker_demo(0); break;
        case 9:  run_worker_demo(0); break;
        case 10: run_rlimit_demo(); break;
        case 11: run_view_logs(); break;
        case 12: run_cleanup_check(); break;
        case 13: run_all(); break;
        case 14: print_usage("./campuscompute"); break;
        }
        print_menu();
    }
    return 0;
}

/* ---------- command-line mode ---------- */

static int bad_usage(const char *prog, const char *msg, const char *arg)
{
    fprintf(stderr, "Error: %s%s%s\n\n", msg, arg ? " " : "", arg ? arg : "");
    print_usage(prog);
    log_event("WARN", "Bad command line: %s %s", msg, arg ? arg : "");
    return 2;
}

static int run_command(int argc, char **argv)
{
    const char *cmd = argv[1];
    const char *opt = argc > 2 ? argv[2] : NULL;
    int q = 2;

    if (argc > 3) return bad_usage(argv[0], "too many arguments for command", cmd);
    if (g_jobs.count == 0) job_load_sample(&g_jobs);     /* repeatable input in CLI mode */

    if (!strcmp(cmd, "help") || !strcmp(cmd, "--help") || !strcmp(cmd, "-h")) { print_usage(argv[0]); return 0; }
    if (!strcmp(cmd, "menu")) return menu_loop();

    if (!strcmp(cmd, "rr") || !strcmp(cmd, "compare")) {
        if (opt && parse_int(opt, 1, 20, &q) != 0)
            return bad_usage(argv[0], "time quantum must be a whole number from 1 to 20, got", opt);
        return (!strcmp(cmd, "rr") ? run_rr(q) : run_compare(q)) == 0 ? 0 : 1;
    }
    if (opt && strcmp(cmd, "banker") && strcmp(cmd, "worker"))
        return bad_usage(argv[0], "command takes no option:", cmd);

    if (!strcmp(cmd, "fcfs"))     return run_fcfs() == 0 ? 0 : 1;
    if (!strcmp(cmd, "priority")) return run_priority() == 0 ? 0 : 1;
    if (!strcmp(cmd, "logger"))   return run_logger_demo() == 0 ? 0 : 1;
    if (!strcmp(cmd, "ipc"))      return run_ipc_demo() == 0 ? 0 : 1;
    if (!strcmp(cmd, "rlimit"))   return run_rlimit_demo() == 0 ? 0 : 1;
    if (!strcmp(cmd, "logs"))     return run_view_logs() == 0 ? 0 : 1;
    if (!strcmp(cmd, "cleanup"))  return run_cleanup_check() == 0 ? 0 : 1;
    if (!strcmp(cmd, "all"))      return run_all();

    if (!strcmp(cmd, "banker")) {
        if (!opt) { int a = run_banker_demo(1); int b = run_banker_demo(2); return (a == 1 && b == 0) ? 0 : 1; }
        if (!strcmp(opt, "safe"))   return run_banker_demo(1) < 0 ? 1 : 0;
        if (!strcmp(opt, "unsafe")) return run_banker_demo(2) < 0 ? 1 : 0;
        if (!strcmp(opt, "custom")) return run_banker_demo(3) < 0 ? 1 : 0;
        return bad_usage(argv[0], "banker option must be safe, unsafe or custom, got", opt);
    }
    if (!strcmp(cmd, "worker")) {
        int mode = 4;
        if (opt) {
            if (!strcmp(opt, "normal")) mode = 1;
            else if (!strcmp(opt, "fail")) mode = 2;
            else if (!strcmp(opt, "crash")) mode = 3;
            else if (!strcmp(opt, "all")) mode = 4;
            else return bad_usage(argv[0], "worker option must be normal, fail, crash or all, got", opt);
        }
        return run_worker_demo(mode) == 0 ? 0 : 1;
    }
    return bad_usage(argv[0], "unknown command", cmd);
}

int main(int argc, char **argv)
{
    int rc;
    if (logger_start(LOG_FILE) != 0)
        fprintf(stderr, "Warning: logging disabled (could not start logger).\n");
    log_event("INFO", "CampusCompute started");

    rc = (argc == 1) ? menu_loop() : run_command(argc, argv);

    log_event("INFO", "CampusCompute exiting (status %d)", rc);
    logger_stop();
    return rc;
}
