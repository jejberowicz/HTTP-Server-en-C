#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <netinet/in.h>

#define THREADPOOL_MAX_QUEUE 1024

typedef struct {
    int               client_fd;
    struct sockaddr_in client_addr;
} tp_task_t;

typedef struct {
    pthread_t        *threads;
    int               n_threads;

    tp_task_t        *queue;
    int               queue_cap;
    int               queue_head;   /* next slot to read  */
    int               queue_tail;   /* next slot to write */
    int               queue_size;   /* current occupancy  */

    pthread_mutex_t   mutex;
    pthread_cond_t    not_empty;
    pthread_cond_t    not_full;

    int               shutdown;
} threadpool_t;

/*
 * Create a pool with `n_threads` worker threads and a bounded task queue.
 * Returns NULL on failure.
 */
threadpool_t *threadpool_create(int n_threads, int queue_cap);

/*
 * Submit a new connection task.  Blocks if the queue is full.
 * Returns 0 on success, -1 if the pool is shutting down.
 */
int threadpool_submit(threadpool_t *pool,
                      int client_fd, struct sockaddr_in *client_addr);

/* Signal all workers to finish their current task and exit. */
void threadpool_destroy(threadpool_t *pool);

#endif /* THREADPOOL_H */
