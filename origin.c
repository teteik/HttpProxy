#include "origin.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

int connect_to_origin(const char* host, int port) {
    struct hostent* host_entry = gethostbyname(host);
    if (!host_entry) return -1;

    int origin_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (origin_fd < 0) return -1;

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr, host_entry->h_addr_list[0], host_entry->h_length);

    if (connect(origin_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(origin_fd);
        return -1;
    }
    return origin_fd;
}

int send_request_to_origin(int origin_fd, const char* path, const char* host) {
    char request[4096];
    int len = snprintf(request, sizeof(request),
        "GET %s HTTP/1.0\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "\r\n", path, host);

    if (len < 0 || len >= (int)sizeof(request)) return -1;
    return (send(origin_fd, request, len, 0) == len) ? 0 : -1;
}