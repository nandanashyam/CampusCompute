#ifndef LOGGER_H
#define LOGGER_H

/* Global asynchronous logger: any thread calls log_event() (producer); one consumer thread
 * writes events to the log file. Bounded queue + mutex + condition variables. */
int  logger_start(const char *path);       /* open file, init sync objects, start consumer */
int  logger_stop(void);                    /* drain queue, join consumer, destroy sync objects */
int  logger_is_running(void);
int  log_event(const char *level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void logger_flush(void);                   /* wait (briefly) until the queue has been written */
void logger_set_echo(int on);              /* demo: print producer/consumer activity */
void logger_set_consumer_delay_ms(int ms); /* demo: slow consumer so the queue fills up */
void logger_set_thread_name(const char *name);
void logger_stats(int *produced, int *consumed);
const char *logger_path(void);

int run_logger_demo(void);                 /* FR3 demonstration, 0 on success */
int run_view_logs(void);                   /* print the tail of the log file */
#endif
