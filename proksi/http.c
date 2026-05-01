#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include "http.h"

size_t http_read_headers(int fd, char *buf, size_t maxlen) {
    size_t total = 0;
    int state = 0;
    char c;

    while (total < maxlen - 1) {
        size_t n = read(fd, &c, 1);
        if (n == 0) {
            break;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        buf[total++] = c;

        switch (state) {
            case 0:
                state = (c == '\r') ? 1 : 0;
                break;
            case 1:
                state = (c == '\n') ? 2 : 0;
                break;
            case 2:
                state = (c == '\r') ? 3 : 0;
                break;
            case 3:
                if (c == '\n') {
                    buf[total] = '\0';
                    return total;
                }
                state = 0;
                break;
        }
    }

    buf[total] = '\0';
    return total;
}
// может ли вернуть тру и при каком условии
int http_get_content_length(const char *headers, size_t *len) {
    const char *p = headers;

    while (*p) {
        const char *line_end = strstr(p, "\r\n");
        if (!line_end) {
            break;
        }

        size_t line_len = line_end - p;

        const char *key = "Content-Length:";
        size_t key_len = strlen(key);

        if (line_len > key_len &&
            strncasecmp(p, "Content-Length:", 15) == 0) {

            const char *v = p + key_len;
            while (*v == ' ' || *v == '\t') {
                v++;
            }

            char *endptr;
            size_t val = strtoull(v, &endptr, 10);

            if (endptr == v) {
                return -1;
            }

            *len = (size_t) val;
            return 0;
        }

        p = line_end + 2;
    }

    return -1;
}
