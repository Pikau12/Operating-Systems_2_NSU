#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

#define MAX_SIZE_REQUEST  1048576
#define MAX_SIZE_RESPONSE 1048576
#define MAX_URL           2048

typedef struct headers_struct {
    struct headers_struct *next;
    char *header;
} headers_t;

size_t http_read_headers(int fd, char *buf, size_t maxlen);

int http_get_content_length(const char *headers, size_t *out_len);

headers_t *http_parse_headers(char *request, size_t *index_start_body);

#endif
