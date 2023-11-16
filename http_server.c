#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>

#include "connection_queue.h"
#include "http.h"

#define BUFSIZE 512
#define LISTEN_QUEUE_LEN 5
#define N_THREADS 5

int keep_going = 1;
const char *serve_dir;

void handle_sigint(int signo) {
    keep_going = 0;
}

// threads continuously dequeue connection file descriptors
void *consumer_loop(void* arg){
    connection_queue_t *queue = (connection_queue_t *) arg;
    while (queue->shutdown == 0){
        // dequeue fd
        int client_fd = connection_dequeue(queue);
        if (client_fd == -1){
            close(client_fd);
            return (void *) -1L;
        }
        char resource[BUFSIZE];

        // extract the resource name
        if ((read_http_request(client_fd, resource)) == -1){
            printf("failed to read http request");
            close(client_fd);
            return (void *) -1L;
        }
        char local_path[BUFSIZE];
        sprintf(local_path, "%s%s", serve_dir, resource);

        // write response back to client
        if ((write_http_response(client_fd, local_path)) == -1){
            printf("failed to write http response");
            close(client_fd);
            return (void *) -1L;
        }
        if (close(client_fd)) {
            perror("close");
            return (void *) -1L;
        }
    }
    return (void *) 0L;
}

int main(int argc, char **argv) {
    // First command is directory to serve, second command is port
    if (argc != 3) {
        printf("Usage: %s <directory> <port>\n", argv[0]);
        return 1;
    }
    serve_dir = argv[1];
    const char *port = argv[2];

    // initialize queue
    connection_queue_t queue;
    if (connection_queue_init(&queue) == -1){
        fprintf(stderr, "failed to initialize connection queue\n");
        return -1;
    }

    // set up signal handler
    struct sigaction act;
    act.sa_handler = handle_sigint;
    sigfillset(&act.sa_mask);
    act.sa_flags = 0;
    if (sigaction(SIGINT, &act, NULL) == -1) {
        perror("sigaction");
        connection_queue_free(&queue);
        return 1;
    }

    // set up addrinfo
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    struct addrinfo *server;
    int res = getaddrinfo(NULL, port, &hints, &server);
    if (res == -1){
        printf("getaddrinfo failed: %s\n", gai_strerror(res));
        connection_queue_free(&queue);
        return -1;
    }

    // create socket file descriptor
    int sock_fd = socket(server->ai_family, server->ai_socktype, server->ai_protocol);
    if (sock_fd == -1){
        perror("socket");
        freeaddrinfo(server);
        connection_queue_free(&queue);
        return -1;
    }

    // bind to port
    if (bind(sock_fd, server->ai_addr, server->ai_addrlen) == -1) {
        perror("bind");
        freeaddrinfo(server);
        close(sock_fd); 
        connection_queue_free(&queue);
        return 1;
    }
    freeaddrinfo(server);

    // designate sock_fd as server socket
    if (listen(sock_fd, LISTEN_QUEUE_LEN) == -1) {
        perror("listen");
        close(sock_fd);
        connection_queue_free(&queue);
        return 1;
    }
    
    // create a mask to block all signals
    sigset_t set;
    sigset_t old_set;
    if (sigfillset(&set) == -1){
        perror("sigfillset");
        close(sock_fd);
        connection_queue_free(&queue);
        return 1; 
    }
    if(sigprocmask(SIG_BLOCK, &set, &old_set) == -1){
        perror("sigprocmask");
        close(sock_fd);
        connection_queue_free(&queue);
        return 1;
    }

    // create n threads
    pthread_t threads[N_THREADS];
    for (int i = 0; i < N_THREADS; i++) {
        int result = pthread_create(threads + i, NULL, consumer_loop, &queue);
        if (result != 0) {
            fprintf(stderr, "pthread_create failed: %s\n", strerror(result));
            for (int j = 0; j < i; j++) {
                pthread_join(threads[j], NULL);
            }
            connection_queue_free(&queue);
            close(sock_fd);
            return 1;
        }
    }

    // return to normal to catch sig_int 
    if(sigprocmask(SIG_SETMASK, &old_set, NULL) == -1){
        perror("sigprocmask");
        close(sock_fd);
        connection_queue_free(&queue);
        return 1;
    }

    // accept clients and pass to threads via queue
    while (keep_going != 0){
        int client_fd = accept(sock_fd, NULL, NULL);
        if (client_fd == -1) {
            if (errno != EINTR) {
                perror("accept");
                close(sock_fd);
                return 1;
            } else {
                break;
            }
        }
        connection_enqueue(&queue, client_fd);
    }

    // cleanup
    connection_queue_shutdown(&queue);
    for (int i = 0; i < N_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    close(sock_fd);
    connection_queue_free(&queue);
    return 0;
}
