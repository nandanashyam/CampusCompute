#ifndef WORKER_H
#define WORKER_H
/* mode: 1 = normal worker, 2 = simulated failure (non-zero exit), 3 = simulated crash (signal),
 *       4 = all three, 0 = ask. Returns 0 if the parent detected every outcome correctly. */
int run_worker_demo(int mode);
#endif
