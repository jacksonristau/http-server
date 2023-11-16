#include <stdio.h>
#include <string.h>
#include "connection_queue.h"

int connection_queue_init(connection_queue_t *queue) {
    queue->length = 0;
    queue->read_idx = 0;
    queue->write_idx = 0;
    queue->shutdown = 0;

    if (pthread_mutex_init(&queue->lock, NULL) != 0) {
        fprintf(stderr, "pthread_mutex_init() failed\n");
        return -1;
    }

    if (pthread_cond_init(&queue->queue_full, NULL) != 0) {
        fprintf(stderr, "pthread_cond_init() failed\n");
        return -1;
    }
    
    if (pthread_cond_init(&queue->queue_empty, NULL) != 0) {
        fprintf(stderr, "pthread_cond_init() failed\n");
        return -1;
    }
    return 0;
}

int connection_enqueue(connection_queue_t *queue, int connection_fd) {
    if (pthread_mutex_lock(&queue->lock) != 0){
        fprintf(stderr, "pthread_cond_init() failed\n");
        return -1;
    }
    while (queue->length == CAPACITY && queue->shutdown == 0) {
        pthread_cond_wait(&queue->queue_full, &queue->lock);
    }

    if (queue->shutdown == 1){
        pthread_mutex_unlock(&queue->lock);
        return -1;
    }

    queue->client_fds[queue->write_idx] = connection_fd;
    queue->length++;
    if (queue->write_idx + 1 == CAPACITY) {
        queue->write_idx = 0;
    }
    else {
        queue->write_idx++;
    }

    pthread_cond_signal(&queue->queue_empty);
    pthread_mutex_unlock(&queue->lock);

    return 0;
}

int connection_dequeue(connection_queue_t *queue) {
    pthread_mutex_lock(&queue->lock);
    while(queue->length == 0 && queue->shutdown == 0){
        pthread_cond_wait(&queue->queue_empty, &queue->lock);
    }
    if (queue->shutdown == 1) {
        pthread_mutex_unlock(&queue->lock);
        return -1;
    }

    int connection_fd = queue->client_fds[queue->read_idx];
    queue->length--;
    if (queue->read_idx + 1 == CAPACITY){
        queue->read_idx = 0;
    }
    else {
        queue->read_idx++;
    }

    pthread_cond_signal(&queue->queue_full);
    pthread_mutex_unlock(&queue->lock);

    return connection_fd;
}

int connection_queue_shutdown(connection_queue_t *queue) {
    pthread_cond_broadcast(&queue->queue_full);
    pthread_cond_broadcast(&queue->queue_empty);
    queue->shutdown = 1;
    return 0;
}

int connection_queue_free(connection_queue_t *queue) {
    pthread_mutex_destroy(&queue->lock);
    pthread_cond_destroy(&queue->queue_full);
    pthread_cond_destroy(&queue->queue_empty);
    return 0;
}
