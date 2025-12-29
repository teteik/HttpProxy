#ifndef ORIGIN_H
#define ORIGIN_H

int connect_to_origin(const char* host, int port);
int send_request_to_origin(int origin_fd, const char* path, const char* host);

#endif