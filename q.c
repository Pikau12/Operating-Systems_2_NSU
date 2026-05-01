
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>

#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define BUFFER_SIZE 8192

#define LISTEN_PORT 8080
#define TARGET_PORT 80

static int proxy_socket = -1;

static void sigint_handler(int sig)
{
    (void)sig;
    printf("\nSIGINT received\n");
    if (proxy_socket >= 0) close(proxy_socket);
    exit(0);
}

static void set_sock_timeouts(int sock, int sec)
{
    struct timeval tv;
    tv.tv_sec = sec;
    tv.tv_usec = 0;

    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

// small helper: case-insensitive prefix match
static int starts_with_ci(const char *s, const char *prefix)
{
    for (; *prefix; s++, prefix++)
    {
        char a = *s, b = *prefix;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
        if (*s == '\0') return 0;
    }
    return 1;
}

// find substring case-insensitively (simple)
static char *strcasestr_simple(char *hay, const char *needle)
{
    size_t nlen = strlen(needle);
    if (nlen == 0) return hay;

    for (char *p = hay; *p; p++)
    {
        if (starts_with_ci(p, needle)) return p;
    }
    return NULL;
}

// read until \r\n\r\n
static int recv_http_header(int sock, char *buf, int bufsize)
{
    int total = 0;

    while (total < bufsize - 1)
    {
        int n = recv(sock, buf + total, bufsize - 1 - total, 0);

        if (n == 0) return 0;
        if (n < 0)
        {
            if (errno == EINTR) continue;
            return -1;
        }

        total += n;
        buf[total] = '\0';

        if (strstr(buf, "\r\n\r\n") != NULL)
            return total;
    }

    errno = EMSGSIZE;
    return -1;
}

// parse method from request line into method_out
static int get_method(const char *req, char *method_out, size_t method_sz)
{
    const char *sp = strchr(req, ' ');
    if (!sp) return -1;
    size_t mlen = (size_t)(sp - req);
    if (mlen == 0 || mlen >= method_sz) return -1;
    memcpy(method_out, req, mlen);
    method_out[mlen] = '\0';
    return 0;
}

// get host either from absolute-form URL (http://host/...) or Host: header
static int get_host(const char *request, char *resolved_host, int from_url_first)
{
    const char *host_start;
    const char *host_end;

    if (from_url_first)
    {
        host_start = strstr(request, "http://");
        if (host_start == NULL) return -1;

        host_start += (int)strlen("http://");
        host_end = strchr(host_start, '/');
        if (host_end == NULL) host_end = strchr(host_start, ' ');
    }
    else
    {
        // case-insensitive "Host:"
        char *h = strcasestr_simple((char*)request, "Host:");
        if (h == NULL) return -1;

        host_start = h + (int)strlen("Host:");
        while (*host_start == ' ') host_start++;

        host_end = strpbrk(host_start, " \r\n");
    }

    if (host_end == NULL)
        host_end = host_start + strlen(host_start);

    size_t host_length = (size_t)(host_end - host_start);
    if (host_length == 0 || host_length >= BUFFER_SIZE) return -1;

    memcpy(resolved_host, host_start, host_length);
    resolved_host[host_length] = '\0';

    // cut :port
    char *colon = strchr(resolved_host, ':');
    if (colon) *colon = '\0';

    return 0;
}

// "GET http://host/path HTTP/1.1" -> "GET /path HTTP/1.1"
static void normalize_request_line(char *buffer, int *len_inout)
{
    int len = *len_inout;
    if (len <= 0) return;

    char *line_end = strstr(buffer, "\r\n");
    if (!line_end) return;

    char *sp1 = strchr(buffer, ' ');
    if (!sp1 || sp1 >= line_end) return;

    char *sp2 = strchr(sp1 + 1, ' ');
    if (!sp2 || sp2 >= line_end) return;

    char *url = sp1 + 1;
    int url_len = (int)(sp2 - url);

    const char prefix[] = "http://";
    int prefix_len = (int)strlen(prefix);

    if (url_len >= prefix_len && strncmp(url, prefix, (size_t)prefix_len) == 0)
    {
        char *after = url + prefix_len;
        char *slash = memchr(after, '/', (size_t)((url + url_len) - after));

        const char *new_url = slash ? slash : "/";
        int new_url_len = slash ? (int)((url + url_len) - slash) : 1;

        int diff = new_url_len - url_len;
        if (len + diff >= BUFFER_SIZE) return;

        memmove(url + new_url_len, url + url_len,
                (size_t)(len - (int)((url + url_len) - buffer)));
        memcpy(url, new_url, (size_t)new_url_len);

        len += diff;
        buffer[len] = '\0';
        *len_inout = len;
    }
}

// remove Proxy-Connection and force Connection: close (more stable)
static void fix_connection_headers(char *buffer, int *len_inout)
{
    int len = *len_inout;

    // remove Proxy-Connection: ...\r\n (case-insensitive)
    char *pc = strcasestr_simple(buffer, "Proxy-Connection:");
    if (pc)
    {
        char *eol = strstr(pc, "\r\n");
        if (eol)
        {
            eol += 2;
            memmove(pc, eol, (size_t)(len - (int)(eol - buffer)));
            len -= (int)(eol - pc);
            buffer[len] = '\0';
        }
    }

    // replace existing Connection: ... with Connection: close
    char *c = strcasestr_simple(buffer, "Connection:");
    if (c)
    {
        char *eol = strstr(c, "\r\n");
        if (eol)
        {
            int prefix = (int)(c - buffer);
            int suffix_off = (int)((eol + 2) - buffer);
            int suffix_len = len - suffix_off;

            char tmp[BUFFER_SIZE];
            int n = snprintf(tmp, sizeof(tmp), "%.*sConnection: close\r\n%.*s",
                             prefix, buffer, suffix_len, buffer + suffix_off);
            if (n > 0 && n < BUFFER_SIZE)
            {
                memcpy(buffer, tmp, (size_t)n);
                len = n;
                buffer[len] = '\0';
            }
        }
    }
    else
    {
        // insert Connection: close before \r\n\r\n
        char *end = strstr(buffer, "\r\n\r\n");
        if (end)
        {
            int head_len = (int)(end - buffer);
            const char *ins = "\r\nConnection: close";
            int ins_len = (int)strlen(ins);

            if (len + ins_len < BUFFER_SIZE)
            {
                memmove(buffer + head_len + ins_len, buffer + head_len, (size_t)(len - head_len));
                memcpy(buffer + head_len, ins, (size_t)ins_len);

                len += ins_len;
                buffer[len] = '\0';
            }
        }
    }

    *len_inout = len;
}

static void send_simple_http_error(int client_socket, int code, const char *msg)
{
    char body[256];
    int blen = snprintf(body, sizeof(body), "%d %s\n", code, msg);

    char resp[512];
    int rlen = snprintf(resp, sizeof(resp),
                        "HTTP/1.1 %d %s\r\n"
                        "Connection: close\r\n"
                        "Content-Type: text/plain\r\n"
                        "Content-Length: %d\r\n"
                        "\r\n"
                        "%.*s",
                        code, msg, blen, blen, body);
    send(client_socket, resp, (size_t)rlen, 0);
}

// connect host:80 using getaddrinfo
static int connect_to_host80(const char *host)
{
    struct addrinfo hints, *res = NULL, *rp = NULL;
    char portstr[16];
    int sock = -1;

    snprintf(portstr, sizeof(portstr), "%d", TARGET_PORT);

    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    hints.ai_protocol = IPPROTO_TCP;

    int err = getaddrinfo(host, portstr, &hints, &res);
    if (err != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next)
    {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock < 0) continue;

        set_sock_timeouts(sock, 10);

        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0)
        {
            freeaddrinfo(res);
            return sock;
        }

        close(sock);
        sock = -1;
    }

    freeaddrinfo(res);
    return -1;
}

static void *handle_client(void *arg)
{
    int client_socket = *(int*)arg;
    free(arg);

    set_sock_timeouts(client_socket, 10);

    char buffer[BUFFER_SIZE];
    char host[BUFFER_SIZE];
    char method[32];

    int bytes_read = recv_http_header(client_socket, buffer, BUFFER_SIZE);

    if (bytes_read == 0)
    {
        close(client_socket);
        return NULL;
    }

    if (bytes_read < 0)
    {
        perror("Error reading client request");
        close(client_socket);
        return NULL;
    }

    printf("Receive request:\n%.*s\n\n", bytes_read, buffer);
    fflush(stdout);

    if (get_method(buffer, method, sizeof(method)) != 0)
    {
        printf("Could not parse method\n");
        close(client_socket);
        return NULL;
    }

    // If browser sends CONNECT (HTTPS), we do NOT support it
    if (strcmp(method, "CONNECT") == 0)
    {
        printf("CONNECT received (HTTPS) -> not supported, returning 501\n");
        fflush(stdout);
        send_simple_http_error(client_socket, 501, "Not Implemented (HTTPS CONNECT not supported)");
        close(client_socket);
        return NULL;
    }

    // parse host: try absolute URL first, then Host header
    if (get_host(buffer, host, 1) != 0)
    {
        if (get_host(buffer, host, 0) != 0)
        {
            printf("Could not parse host\n");
            close(client_socket);
            return NULL;
        }
    }

    printf("Host: %s\n", host);
    fflush(stdout);

    // normalize and fix headers
    normalize_request_line(buffer, &bytes_read);
    fix_connection_headers(buffer, &bytes_read);

    int target_socket = connect_to_host80(host);
    if (target_socket < 0)
    {
        perror("Error connecting to target server");
        close(client_socket);
        return NULL;
    }

    // send first chunk
    int total_sent = 0;
    while (total_sent < bytes_read)
    {
        int n = send(target_socket, buffer + total_sent, (size_t)(bytes_read - total_sent), 0);
        if (n <= 0)
        {
            perror("Error sending initial request to server");
            close(client_socket);
            close(target_socket);
            return NULL;
        }
        total_sent += n;
    }

    fd_set readfds;
    int max_fd = (client_socket > target_socket) ? client_socket : target_socket;
    int client_closed = 0, server_closed = 0;

    while (!client_closed && !server_closed)
    {
        FD_ZERO(&readfds);
        if (!client_closed) FD_SET(client_socket, &readfds);
        if (!server_closed) FD_SET(target_socket, &readfds);

        if (select(max_fd + 1, &readfds, NULL, NULL, NULL) < 0)
        {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        // client -> server
        if (!client_closed && FD_ISSET(client_socket, &readfds))
        {
            int nread = recv(client_socket, buffer, BUFFER_SIZE, 0);
            if (nread == 0)
            {
                client_closed = 1;
                shutdown(target_socket, SHUT_WR);
            }
            else if (nread < 0)
            {
                perror("Error reading from client");
                client_closed = 1;
                shutdown(target_socket, SHUT_WR);
            }
            else
            {
                int sent = 0;
                while (sent < nread)
                {
                    int ns = send(target_socket, buffer + sent, (size_t)(nread - sent), 0);
                    if (ns <= 0)
                    {
                        perror("Error sending to server");
                        server_closed = 1;
                        break;
                    }
                    sent += ns;
                }
            }
        }

        // server -> client
        if (!server_closed && FD_ISSET(target_socket, &readfds))
        {
            int nread = recv(target_socket, buffer, BUFFER_SIZE, 0);
            if (nread == 0)
            {
                server_closed = 1;
                shutdown(client_socket, SHUT_WR);
            }
            else if (nread < 0)
            {
                perror("Error reading from server");
                server_closed = 1;
                shutdown(client_socket, SHUT_WR);
            }
            else
            {
                int sent = 0;
                while (sent < nread)
                {
                    int ns = send(client_socket, buffer + sent, (size_t)(nread - sent), 0);
                    if (ns <= 0)
                    {
                        perror("Error sending to client");
                        client_closed = 1;
                        break;
                    }
                    sent += ns;
                }
            }
        }
    }

    close(client_socket);
    close(target_socket);
    return NULL;
}

int main(void)
{
    signal(SIGINT, sigint_handler);
    signal(SIGPIPE, SIG_IGN);

    struct sockaddr_in proxy_addr;
    pthread_t thread_id;

    proxy_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (proxy_socket < 0)
    {
        perror("Error creating socket");
        exit(1);
    }

    int opt = 1;
    setsockopt(proxy_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&proxy_addr, 0, sizeof(proxy_addr));
    proxy_addr.sin_family = AF_INET;
    proxy_addr.sin_addr.s_addr = INADDR_ANY;
    proxy_addr.sin_port = htons(LISTEN_PORT);

    if (bind(proxy_socket, (struct sockaddr *)&proxy_addr, sizeof(proxy_addr)) < 0)
    {
        perror("Error binding socket");
        close(proxy_socket);
        exit(1);
    }

    if (listen(proxy_socket, SOMAXCONN) < 0)
    {
        perror("Error listening for connections");
        close(proxy_socket);
        exit(1);
    }

    printf("HTTP proxy server listening on port %d\n", LISTEN_PORT);
    fflush(stdout);

    while (1)
    {
        int client_socket = accept(proxy_socket, NULL, NULL);
        if (client_socket < 0)
        {
            perror("Error accepting connection");
            continue;
        }

        int *pclient = (int *)malloc(sizeof(int));
        if (!pclient)
        {
            perror("malloc");
            close(client_socket);
            continue;
        }
        *pclient = client_socket;

        if (pthread_create(&thread_id, NULL, handle_client, (void *)pclient) != 0)
        {
            perror("Error creating thread");
            close(client_socket);
            free(pclient);
            continue;
        }

        pthread_detach(thread_id);
    }

    close(proxy_socket);
    return 0;
}

