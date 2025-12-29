#include "client_handler.h"
#include "http_parser.h"
#include "origin.h"
#include "cache.h"
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <strings.h>

#define BUFFER_SIZE 8192

typedef struct {
    CacheEntry* entry;
    char* url;
} FetcherArgs;

void send_cached_response(int client_fd, CacheEntry* entry) {
    char header[1024];
    int len = snprintf(header, sizeof(header),
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        entry->content_type ? entry->content_type : "application/octet-stream",
        entry->size
    );
    send(client_fd, header, len, 0);
    send(client_fd, entry->data, entry->size, 0);
}

void stream_from_cache_to_client(int client_fd, CacheEntry* entry) {
    char header[1024];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Connection: close\r\n"
        "\r\n",
        entry->content_type ? entry->content_type : "application/octet-stream"
    );
    send(client_fd, header, header_len, 0);

    size_t offset = 0;
    char buf[BUFFER_SIZE];

    while (1) {
        pthread_mutex_lock(&entry->mtx);
        while (offset >= entry->size && !entry->complete) {
            pthread_cond_wait(&entry->cv, &entry->mtx);
        }
        size_t to_read = (entry->size - offset > BUFFER_SIZE) ? BUFFER_SIZE : (entry->size - offset);
        int is_complete = entry->complete;
        pthread_mutex_unlock(&entry->mtx);

        if (to_read > 0) {
            memcpy(buf, entry->data + offset, to_read);
            send(client_fd, buf, to_read, 0);
            offset += to_read;
        }
        if (is_complete) break;
    }
}

void* origin_fetcher_thread(void* arg) {
    FetcherArgs* a = (FetcherArgs*)arg;
    CacheEntry* entry = a->entry;

    char host[MAX_HOST_LEN], path[MAX_PATH_LEN];
    int port;
    if (extract_url_components(a->url, host, &port, path) != 0) {
        free(a->url);
        free(a);
        return NULL;
    }

    int origin_fd = connect_to_origin(host, port);
    if (origin_fd < 0) {
        free(a->url);
        free(a);
        return NULL;
    }

    if (send_request_to_origin(origin_fd, path, host) != 0) {
        close(origin_fd);
        free(a->url);
        free(a);
        return NULL;
    }

    char header_buf[8192];
    int status_code;
    char* content_type = NULL;
    if (parse_http_response(origin_fd, &status_code, &content_type, header_buf, sizeof(header_buf)) == 0) {
        cache_mark_complete(entry, status_code, content_type);
        if (status_code == 200) {
            char body_buf[BUFFER_SIZE];
            ssize_t n;
            while ((n = recv(origin_fd, body_buf, sizeof(body_buf), 0)) > 0) {
                cache_append(entry, body_buf, n);
            }
            pthread_mutex_lock(&entry->mtx);
            entry->complete = 1;
            pthread_cond_broadcast(&entry->cv);
            pthread_mutex_unlock(&entry->mtx);
        }
    }

    close(origin_fd);
    free(content_type);
    free(a->url);
    free(a);
    return NULL;
}

void* handle_single_client(void* arg) {
    int client_fd = *(int*)arg;
    free(arg);
    char request_buffer[BUFFER_SIZE];
    ssize_t n = recv(client_fd, request_buffer, sizeof(request_buffer) - 1, 0);
    if (n <= 0) { close(client_fd); return NULL; }
    request_buffer[n] = '\0';

    char* end_of_line = strstr(request_buffer, "\r\n");
    if (!end_of_line) { close(client_fd); return NULL; }
    *end_of_line = '\0';

    char method[16], full_url[MAX_URL_LEN], version[16];
    if (sscanf(request_buffer, "%15s %1023s %15s", method, full_url, version) != 3) {
        const char* err = "HTTP/1.0 400 Bad Request\r\n\r\n";
        send(client_fd, err, strlen(err), 0);
        close(client_fd);
        return NULL;
    }

    if (strcmp(method, "GET") != 0) {
        const char* err = "HTTP/1.0 501 Not Implemented\r\n\r\n";
        send(client_fd, err, strlen(err), 0);
        close(client_fd);
        return NULL;
    }

    CacheEntry* entry = cache_get_or_create(full_url);

    pthread_mutex_lock(&entry->mtx);
    int is_complete = entry->complete;
    int is_200 = (entry->status_code == 200);
    pthread_mutex_unlock(&entry->mtx);

    if (is_complete && is_200) {
        send_cached_response(client_fd, entry);
        close(client_fd);
        return NULL;
    }

    int need_fetch = 0;
    pthread_mutex_lock(&entry->mtx);
    if (!entry->complete && entry->size == 0) {
        need_fetch = 1;
    }
    pthread_mutex_unlock(&entry->mtx);

    if (need_fetch) {
        fprintf(stderr, "[PROXY] Fetching from origin: %s\n", full_url);
        fflush(stderr);

        FetcherArgs* args = malloc(sizeof(FetcherArgs));
        args->entry = entry;
        args->url = strdup(full_url);
        pthread_t fetcher;
        pthread_create(&fetcher, NULL, origin_fetcher_thread, args);
        pthread_detach(fetcher);
    }

    stream_from_cache_to_client(client_fd, entry);
    close(client_fd);
    return NULL;
}