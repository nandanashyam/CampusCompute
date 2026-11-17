# CampusCompute

**Intelligent job scheduling and resource management**, a command-line prototype written in C that demonstrates core operating-system concepts on Linux (tested on Ubuntu / WSL Ubuntu). Built for the Operating Systems (UE25CS242A) Level 3 mini-project.

## Features

| Area | What it demonstrates |
|---|---|
| Job management | Validated job input (ID, arrival, burst, priority) and status tracking |
| CPU scheduling | FCFS, Round Robin, non-preemptive Priority, with timeline, CT/TAT/WT and averages, plus a side-by-side comparison |
| Concurrent logger | POSIX threads, bounded producer-consumer queue, mutex and condition variables, clean shutdown |
| IPC | POSIX shared memory plus named semaphores between a sender and a receiver process |
| Deadlock avoidance | Banker's safety algorithm with safe and unsafe test cases and a custom-input mode |
| Worker monitoring | `fork()` and `waitpid()` with normal exit, simulated non-zero exit and simulated signal termination |
| Resource limits | `setrlimit()` with `RLIMIT_NOFILE` and `RLIMIT_CPU` |
| Cleanup | Verification that children, threads and program-created IPC objects are released |

## Requirements

- Linux, Ubuntu or WSL Ubuntu
- GCC and Make: `sudo apt update && sudo apt install build-essential`

## Build and run

```bash
make                  # builds ./campuscompute
./campuscompute       # interactive menu
./campuscompute help  # list all commands
make clean            # remove binary, objects and campuscompute.log
```

## Commands

Each feature can be run and tested independently:

```bash
./campuscompute fcfs
./campuscompute rr [quantum]          # quantum 1-20, default 2
./campuscompute priority
./campuscompute compare [quantum]
./campuscompute logger
./campuscompute ipc
./campuscompute banker [safe|unsafe|custom]
./campuscompute worker [normal|fail|crash|all]
./campuscompute rlimit
./campuscompute logs
./campuscompute cleanup
./campuscompute all                   # integrated demo with pass/fail summary
```

Invalid commands or options print an error followed by the usage text (exit status 2). CLI commands use a fixed sample job set so results are repeatable.

## Sample output

FCFS on the built-in job set:

```
Job   Arrival  Burst  Prio  Start  CT    TAT   WT    Status
P1    0        5      3     0      5     5     0     COMPLETED
P2    1        3      1     5      8     7     4     COMPLETED
P3    2        8      4     8      16    14    6     COMPLETED
P4    3        6      1     16     22    19    13    COMPLETED
P5    25       2      2     25     27    2     0     COMPLETED
Average turnaround time = 9.40
Average waiting time    = 4.60
```

Banker's algorithm, safe state:

```
SAFE STATE. Safe sequence: <P1, P3, P4, P0, P2>
```

## Design notes

- **Limits:** ID 1-9999, arrival 0-1000, burst 1-100, priority 1-10 (1 = highest), up to 20 jobs.
- **Tie-breaking:** earlier arrival first, then smaller job ID, for every scheduler.
- **Round Robin:** a job arriving at the instant a quantum expires is queued before the pre-empted job.
- **Simulation:** scheduling results come from the program's own algorithms; the Linux scheduler is not controlled.
- **Logger:** 8-slot bounded queue, one consumer thread, `not_full` / `not_empty` condition variables. Shutdown drains the queue and joins the thread. Output goes to `campuscompute.log`.
- **IPC:** shared memory `/campuscompute_shm` with semaphores `/campuscompute_sem_empty` and `/campuscompute_sem_full`. Only these three names are ever unlinked. Checksums confirm the data received matches the data sent.
- **Workers:** failures are simulated (exit status 3, or the worker raising SIGTERM on itself) and labelled as such. A non-zero exit status is not necessarily a crash.
- **Fork safety:** child processes never call the logger, because its thread does not exist after `fork()`.

## Project structure

```
.
├── main.c          # menu, CLI dispatch, integrated demo
├── util.c/.h       # safe input and parsing helpers
├── job.c/.h        # job data and validation
├── scheduler.c/.h  # FCFS, Round Robin, Priority, comparison
├── logger.c/.h     # producer-consumer logger (pthreads)
├── ipc.c/.h        # shared memory + named semaphores
├── banker.c/.h     # Banker's safety algorithm
├── worker.c/.h     # fork/waitpid worker monitoring
├── resources.c/.h  # setrlimit demos and cleanup check
├── Makefile
└── README.md
```

## Checking for leftovers

```bash
./campuscompute cleanup
ls /dev/shm                       # no campuscompute entries
ps aux | grep '[c]ampuscompute'   # prints nothing
```
