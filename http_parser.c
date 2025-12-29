#include "http_parser.h"
#include <string.h>      
#include <stdlib.h>     
#include <stdio.h>       
#include <unistd.h>      
#include <sys/socket.h>  

int extract_url_components(const char* full_url, char* out_host, int* out_port, char* out_path) {
    if (strncmp(full_url, "http://", 7) != 0) return -1;
    const char* start = full_url + 7;
    const char* path_start = strchr(start, '/');
    const char* port_start = strchr(start, ':');
    const char* host_end = path_start ? path_start : start + strlen(start);

    if (port_start && port_start < host_end) {
        int host_len = port_start - start;
        if (host_len >= MAX_HOST_LEN) return -1;
        strncpy(out_host, start, host_len);
        out_host[host_len] = '\0';
        *out_port = atoi(port_start + 1);
    } else {
        int host_len = host_end - start;
        if (host_len >= MAX_HOST_LEN) return -1;
        strncpy(out_host, start, host_len);
        out_host[host_len] = '\0';
        *out_port = 80;
    }

    if (path_start) {
        strncpy(out_path, path_start, MAX_PATH_LEN - 1);
        out_path[MAX_PATH_LEN - 1] = '\0';
    } else {
        strcpy(out_path, "/");
    }
    return 0;
}

int parse_http_response(int fd, int* out_status, char** out_content_type, char* header_buf, size_t buf_size) {
    ssize_t total = 0;
    ssize_t n;
    while (total < (ssize_t)buf_size - 1) {
        n = recv(fd, header_buf + total, 1, 0);
        if (n <= 0) break;
        total += n;
        header_buf[total] = '\0';
        if (total >= 4 && strcmp(&header_buf[total-4], "\r\n\r\n") == 0) break;
    }

    if (sscanf(header_buf, "HTTP/1.0 %d", out_status) != 1 &&
        sscanf(header_buf, "HTTP/1.1 %d", out_status) != 1) {
        *out_status = 0;
        return -1;
    }

    char* content_type_line = strstr(header_buf, "Content-Type:");
    if (content_type_line) {
        char* start = content_type_line + 13;
        char* end = strchr(start, '\r');
        if (end) {
            int len = end - start;
            *out_content_type = malloc(len + 1);
            strncpy(*out_content_type, start, len);
            (*out_content_type)[len] = '\0';
        }
    }

    return 0;
}