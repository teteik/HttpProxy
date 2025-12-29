#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "cache.h"
#include "client_handler.h"

#define PROXY_PORT 80 

int create_listening_socket() {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); exit(1); }

    int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(PROXY_PORT);

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(1);
    }
    if (listen(listen_fd, 64) < 0) {
        perror("listen"); exit(1);
    }
    return listen_fd;
}

int main() {
    g_cache.max_size = CACHE_MAX_SIZE;
    pthread_mutex_init(&g_cache.mtx, NULL);
    pthread_cond_init(&g_cache.cv, NULL); 
    
    pthread_t gc_thread;
    pthread_create(&gc_thread, NULL, cache_gc_thread, NULL);
    pthread_detach(gc_thread);

    int listen_fd = create_listening_socket();
    printf("Cache proxy listening on port %d\n", PROXY_PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd < 0) continue;

        int* p_fd = malloc(sizeof(int));
        *p_fd = client_fd;
        pthread_t client_thread;
        if (pthread_create(&client_thread, NULL, handle_single_client, p_fd) != 0) {
            free(p_fd);
            close(client_fd);
        } else {
            pthread_detach(client_thread);
        }
    }

    close(listen_fd);
    return 0;
}