#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include <stddef.h>      
#include <sys/types.h>   

#define MAX_HOST_LEN 256
#define MAX_PATH_LEN 1024
#define MAX_URL_LEN 1024


int extract_url_components(const char* full_url, char* out_host, int* out_port, char* out_path);
int parse_http_response(int fd, int* out_status, char** out_content_type, char* header_buf, size_t buf_size);

#endif