#ifndef IPC_H
#define IPC_H
int run_ipc_demo(void);                 /* FR4: shared memory + named semaphores, 0 on success */
int ipc_verify_cleanup(int verbose);    /* 0 if no program-created IPC object remains */
#endif
