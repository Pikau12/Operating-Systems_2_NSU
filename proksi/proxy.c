#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <ctype.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdint.h>

#define PORT 8080
#define MAX_CLIENTS 1000

#include "http.h"

int flag_is_working = 1;
int server_socket;

int resolve_hostname(const char *hostname, char *ip, size_t ip_len) {
    struct addrinfo hints, *res, *p;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(hostname, NULL, &hints, &res) != 0) {
        perror("getaddrinfo");
        return -1;
    }

    if (res == NULL) {
        return -1;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        struct sockaddr_in *addr = (struct sockaddr_in *) p->ai_addr;
        inet_ntop(AF_INET, &addr->sin_addr, ip, ip_len);
        break;
    }

    freeaddrinfo(res);
    return 0;
}

static int write_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    size_t off = 0;

    while (off < len) {
        ssize_t w = write(fd, p + off, len - off);
        if (w < 0) {
            if (errno == EINTR) continue;  
            return -1;                      
        }
        if (w == 0) {
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

static ssize_t read_retry(int fd, void *buf, size_t len) {
    ssize_t r;
    while (1) {
        r = read(fd, buf, len);
        if (r < 0 && errno == EINTR)
            continue;
        return r;
    }
}

void send_http_error(int client_fd, int code, const char *message) {
    char buffer[512];

    int len = snprintf(
            buffer, sizeof(buffer),
            "HTTP/1.0 %d %s\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            code, message,
            strlen(message),
            message
    );

    if (write_all(client_fd, buffer, (size_t)len) != 0) {
        perror("write");
    }
}

int connect_to_host(int client_fd, char *host, int port) {
    int socket_host = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_host < 0) {
        return -1;
    }

    struct sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_port = htons(port)
    };
    if (!inet_aton(host, &addr.sin_addr)) {
        size_t ip_len = 16;
        char ip[ip_len];
        if (resolve_hostname(host, ip, ip_len) != 0) {
            send_http_error(client_fd, 502, "Bad Gateway");
            close(socket_host);
            return -1;
        }
        if (!inet_aton(ip, &addr.sin_addr)) {
            perror("resolve dns");
            send_http_error(client_fd, 502, "Bad Gateway");
            close(socket_host);
            return -1;
        }
        
    }

    if (connect(socket_host, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        send_http_error(client_fd, 502, "Bad Gateway");
        close(socket_host);
        return -1;
    }

    return socket_host;
}

void another_method(int client_fd, int socket_host, char *url, char *method,
                    char *request, size_t size_request, char *new_request)
{

    if (write_all(socket_host, new_request, size_request) != 0) {
        perror("write");
        send_http_error(client_fd, 500, "Internal Server Error");
        return;
    }

    size_t content_length = 0;
    int has_len = (http_get_content_length(request, &content_length) == 0);
    size_t x = 0;

    if (has_len && content_length > 0) {
        while (x < content_length) {
            size_t to_read = MAX_SIZE_REQUEST;
            if (content_length - x < to_read)
                to_read = content_length - x;

            ssize_t r = read_retry(client_fd, request, to_read);
            if (r == 0) break;
            if (r < 0) {
                perror("read client");
                send_http_error(client_fd, 400, "Bad Request");
                return;
            }

            size_t got = (size_t)r;
            x += got;

            if (write_all(socket_host, request, got) != 0) {
                perror("write to host");
                send_http_error(client_fd, 500, "Internal Server Error");
                return;
            }
        }
    }

    int size_response;
    char response[MAX_SIZE_RESPONSE];

    size_response = (int)http_read_headers(socket_host, response, MAX_SIZE_RESPONSE);
    if (size_response < 0) {
        perror("read");
        send_http_error(client_fd, 500, "Internal Server Error");
        return;
    }

    if (write_all(client_fd, response, (size_t)size_response) != 0) {
        perror("write");
        return;
    }

    content_length = 0;
    has_len = (http_get_content_length(response, &content_length) == 0);

    x = 0;
    if (has_len) {
        while (x < content_length) {
            size_t to_read = MAX_SIZE_RESPONSE;
            if (content_length - x < to_read) to_read = content_length - x;

            ssize_t r = read_retry(socket_host, response, to_read);
            if (r == 0) break;
            if (r < 0) {
                perror("read host");
                send_http_error(client_fd, 502, "Bad Gateway");
                return;
            }

            x += (size_t)r;

            if (write_all(client_fd, response, (size_t)r) != 0) {
                perror("write to client");
                return;
            }
        }
    } else {
        while (1) {
            ssize_t r = read_retry(socket_host, response, MAX_SIZE_RESPONSE);
            if (r == 0) break;
            if (r < 0) {
                perror("read host");
                send_http_error(client_fd, 502, "Bad Gateway");
                return;
            }

            if (write_all(client_fd, response, (size_t)r) != 0) {
                perror("write to client");
                return;
            }
        }
    }
}

void *client_handler(void *arg) {
    int client_fd = (int) (intptr_t) arg;
    char request[MAX_SIZE_REQUEST + 1];

    size_t size_request = http_read_headers(client_fd, request, MAX_SIZE_REQUEST);
    if (size_request <= 0) {
        close(client_fd);
        return NULL;
    }
    request[size_request] = 0;

    printf("Receive request:\n%s\n\n", request);

    char method[8];
    char url[MAX_URL];
    char version[9];
    if (sscanf(request, "%7s %2047s %8s", method, url, version) != 3) {
        send_http_error(client_fd, 400, "Bad Request");
        close(client_fd);
        return NULL;
    }

/*     if (strcmp(version, "HTTP/1.0") != 0) {
        send_http_error(client_fd, 505, "HTTP Version Not Supported");
        close(client_fd);
        return NULL;
    } */

    if (strcmp(version, "HTTP/1.0") != 0 && strcmp(version, "HTTP/1.1") != 0) {
        send_http_error(client_fd, 505, "HTTP Version Not Supported");
        close(client_fd);
        return NULL;
    }

    char host[MAX_URL / 2], path[MAX_URL / 2];
    int port = 80;

    if (strncmp(url, "http://", 7) != 0) {
        send_http_error(client_fd, 400, "Bad Request");
        close(client_fd);
        return NULL;
    }

    const char *pointer_url = url + 7;
    const char *slash = strchr(pointer_url, '/');
    if (slash == NULL) {
        strcpy(path, "/");
        slash = pointer_url + strlen(pointer_url);
    } else {
        snprintf(path, sizeof(path), "%s", slash);
    }

    const char *colon = memchr(pointer_url, ':', slash - pointer_url);
    if (colon != NULL) {
        snprintf(host, sizeof(host), "%.*s", (int) (colon - pointer_url), pointer_url);
        port = atoi(colon + 1);
    } else {
        snprintf(host, sizeof(host), "%.*s", (int) (slash - pointer_url), pointer_url);
    }

    int socket_host = connect_to_host(client_fd, host, port);
    if (socket_host == -1) {
        close(client_fd);
        return NULL;
    }

    char *new_request = request + (slash - url);
    sprintf(new_request, "%s", method);
    new_request[strlen(method)] = ' ';

    size_request -= (slash - url);

    another_method(client_fd, socket_host, url, method, request, size_request, new_request);

    close(socket_host);
    close(client_fd);

    return NULL;
}

void handle_sigint(int sig) {
    flag_is_working = 0;
}

int main() {
    struct sigaction sa;
    signal(SIGPIPE, SIG_IGN);
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    if ((server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_port = htons(PORT),
            .sin_addr.s_addr = INADDR_ANY
    };

    if (bind(server_socket, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, MAX_CLIENTS) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Proxy on port %d\n", PORT);

    while (flag_is_working) {
        int client_fd = accept(server_socket, NULL, NULL);
        if (client_fd < 0) {
            flag_is_working = 0;
            break;
        }
        if (!flag_is_working) {
            close(client_fd);
            break;
        }
        pthread_t thread;
        pthread_create(&thread, NULL, client_handler, (void *) (intptr_t) client_fd);
        pthread_detach(thread);
    }
    close(server_socket);
}
