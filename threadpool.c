#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "threadpool.h"

/* Forward declaration — defined in main.c */
void handle_connection(int client_fd, struct sockaddr_in *client_addr);

static void *worker(void *arg) {
    threadpool_t *pool = (threadpool_t *)arg;

    while (1) {
        pthread_mutex_lock(&pool->mutex);

        /* Wait until there is work or the pool is shutting down */
        while (pool->queue_size == 0 && !pool->shutdown)
            pthread_cond_wait(&pool->not_empty, &pool->mutex);

        if (pool->shutdown && pool->queue_size == 0) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }

        /* Dequeue */
        tp_task_t task = pool->queue[pool->queue_head];
        pool->queue_head = (pool->queue_head + 1) % pool->queue_cap;
        pool->queue_size--;

        pthread_cond_signal(&pool->not_full);
        pthread_mutex_unlock(&pool->mutex);

        /* Process outside the lock */
        handle_connection(task.client_fd, &task.client_addr);
    }

    return NULL;
}

threadpool_t *threadpool_create(int n_threads, int queue_cap) {
    if (n_threads <= 0 || queue_cap <= 0 || queue_cap > THREADPOOL_MAX_QUEUE)
        return NULL;

    threadpool_t *pool = calloc(1, sizeof(*pool));
    if (!pool)
        return NULL;

    pool->queue = calloc((size_t)queue_cap, sizeof(tp_task_t));
    pool->threads = calloc((size_t)n_threads, sizeof(pthread_t));
    if (!pool->queue || !pool->threads)
        goto fail;

    pool->n_threads  = n_threads;
    pool->queue_cap  = queue_cap;

    pthread_mutex_init(&pool->mutex,     NULL);
    pthread_cond_init (&pool->not_empty, NULL);
    pthread_cond_init (&pool->not_full,  NULL);

    for (int i = 0; i < n_threads; i++) {
        if (pthread_create(&pool->threads[i], NULL, worker, pool) != 0)
            goto fail;
    }

    return pool;

fail:
    free(pool->queue);
    free(pool->threads);
    free(pool);
    return NULL;
}

int threadpool_submit(threadpool_t *pool,
                      int client_fd, struct sockaddr_in *client_addr) {
    pthread_mutex_lock(&pool->mutex);

    while (pool->queue_size == pool->queue_cap && !pool->shutdown)
        pthread_cond_wait(&pool->not_full, &pool->mutex);

    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->mutex);
        return -1;
    }

    pool->queue[pool->queue_tail].client_fd   = client_fd;
    pool->queue[pool->queue_tail].client_addr = *client_addr;
    pool->queue_tail = (pool->queue_tail + 1) % pool->queue_cap;
    pool->queue_size++;

    pthread_cond_signal(&pool->not_empty);
    pthread_mutex_unlock(&pool->mutex);
    return 0;
}

void threadpool_destroy(threadpool_t *pool) {
    pthread_mutex_lock(&pool->mutex);
    pool->shutdown = 1;
    pthread_cond_broadcast(&pool->not_empty);
    pthread_mutex_unlock(&pool->mutex);

    for (int i = 0; i < pool->n_threads; i++)
        pthread_join(pool->threads[i], NULL);

    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->not_empty);
    pthread_cond_destroy(&pool->not_full);

    free(pool->queue);
    free(pool->threads);
    free(pool);
}
