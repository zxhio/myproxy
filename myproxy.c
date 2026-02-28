#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define close(fd) closesocket(fd)
#define read(fd, buf, len) recv(fd, buf, len, 0)
#define write(fd, buf, len) send(fd, buf, len, 0)
#define SHUT_RD SD_RECEIVE
#define SHUT_WR SD_SEND
#define SHUT_RDWR SD_BOTH
typedef int socklen_t;
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <ev.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUF_SIZE 1024 * 32

typedef struct {
    int verbose;
    char *listen_addr;
    int listen_port;
    char *backend_addr;
    int backend_port;
} config_t;

static config_t g_cfg = {0, NULL, 0, NULL, 0};

#define LOG(level, fmt, ...)                                                                       \
    do {                                                                                           \
        if (g_cfg.verbose >= level)                                                                \
            printf(fmt "\n", ##__VA_ARGS__);                                                       \
    } while (0)
#define LOG_DEBUG(fmt, ...) LOG(1, fmt, ##__VA_ARGS__) // -v: connection details and traffic stats
#define LOG_TRACE(fmt, ...) LOG(2, fmt, ##__VA_ARGS__) // -vv: detailed read/write operations
#define LOG_INFO(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)

typedef struct conn_pair conn_pair_t;

typedef struct {
    conn_pair_t *pair;
    int to_fd;
    int from_fd;
    ev_io rw, ww;
    unsigned char buf[BUF_SIZE];
    char from_label[48];
    char to_label[48];
    size_t len, sent;
    int eof;
    size_t total_read;
    size_t total_write;
} broker_t;

struct conn_pair {
    struct ev_loop *loop;
    int client_fd;
    int backend_fd;
    broker_t *fwd;
    broker_t *bwd;
    int done;
    ev_tstamp start_time;
};

#ifdef _WIN32
static void set_nonblock(int fd)
{
    u_long mode = 1;
    ioctlsocket(fd, FIONBIO, &mode);
}
#else
static void set_nonblock(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK); }
#endif

static char *format_size(char *buf, size_t buf_len, size_t bytes)
{
    if (bytes < 1024)
        snprintf(buf, buf_len, "%zu B", bytes);
    else if (bytes < 1024 * 1024)
        snprintf(buf, buf_len, "%.2f KiB", bytes / 1024.0);
    else if (bytes < 1024 * 1024 * 1024)
        snprintf(buf, buf_len, "%.2f MiB", bytes / (1024.0 * 1024));
    else
        snprintf(buf, buf_len, "%.2f GiB", bytes / (1024.0 * 1024 * 1024));
    return buf;
}

static void broker_done(broker_t *b)
{
    conn_pair_t *p = b->pair;
    ev_io_stop(p->loop, &b->rw);
    ev_io_stop(p->loop, &b->ww);
    LOG_TRACE("%s -> %s: done", b->from_label, b->to_label);

    if (++p->done != 2)
        return;

    ev_tstamp now = ev_time();
    double duration = now - p->start_time;

    char fwd_buf[32], fwd_rate_buf[32];
    char bwd_buf[32], bwd_rate_buf[32];

    LOG_INFO("Connection %s <-> %s closed (%.3fs)", b->from_label, b->to_label, duration);
    LOG_DEBUG(
        "  forward: %s (%s/s), backward: %s (%s/s)",
        format_size(fwd_buf, sizeof(fwd_buf), p->fwd->total_write),
        format_size(fwd_rate_buf, sizeof(fwd_rate_buf), (size_t)(p->fwd->total_write / duration)),
        format_size(bwd_buf, sizeof(bwd_buf), p->bwd->total_write),
        format_size(bwd_rate_buf, sizeof(bwd_rate_buf), (size_t)(p->bwd->total_write / duration)));
    close(p->client_fd);
    close(p->backend_fd);
    free(p->fwd);
    free(p->bwd);
    free(p);
}

static void on_writable(struct ev_loop *loop, ev_io *w, int revents)
{
    broker_t *b = (broker_t *)w->data;
    (void)loop;
    (void)revents;

    if (b->sent == b->len) {
        b->len = b->sent = 0;
        ev_io_stop(EV_A_ & b->ww);
        if (b->eof) {
            shutdown(b->to_fd, SHUT_WR);
            broker_done(b);
        } else if (!ev_is_active(&b->rw)) {
            ev_io_start(EV_A_ & b->rw);
        }
        return;
    }

    ssize_t n = write(b->to_fd, b->buf + b->sent, b->len - b->sent);
    if (n > 0) {
        b->total_write += n;
        b->sent += n;
        char size_buf[32];
        LOG_TRACE("%s -> %s, write %zd bytes (total: %s)", b->to_label, b->from_label, n,
                  format_size(size_buf, sizeof(size_buf), b->total_write));

        if (b->sent == b->len) {
            b->len = b->sent = 0;
            ev_io_stop(EV_A_ & b->ww);
            if (b->eof) {
                shutdown(b->to_fd, SHUT_WR);
                broker_done(b);
            } else if (!ev_is_active(&b->rw)) {
                ev_io_start(EV_A_ & b->rw);
            }
        }
    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        LOG_ERROR("%s -> %s, write error: %s", b->to_label, b->from_label, strerror(errno));
        shutdown(b->from_fd, SHUT_RD);
        shutdown(b->to_fd, SHUT_WR);
        ev_io_stop(EV_A_ & b->ww);
        ev_io_stop(EV_A_ & b->rw);
        broker_done(b);
    }
}

static void on_readable(struct ev_loop *loop, ev_io *w, int revents)
{
    broker_t *b = (broker_t *)w->data;
    (void)loop;
    (void)revents;

    // Prevent read returning 0 and being mistaken for EOF when buffer is full
    if (b->len == BUF_SIZE) {
        ev_io_stop(EV_A_ & b->rw);
        return;
    }

    ssize_t n = read(b->from_fd, b->buf + b->len, BUF_SIZE - b->len);
    if (n > 0) {
        b->total_read += n;
        b->len += n;
        char size_buf[32];
        LOG_TRACE("%s -> %s, read %zd bytes (total: %s)", b->from_label, b->to_label, n,
                  format_size(size_buf, sizeof(size_buf), b->total_read));

        if (b->len == BUF_SIZE)
            ev_io_stop(EV_A_ & b->rw);
        if (!ev_is_active(&b->ww))
            ev_io_start(EV_A_ & b->ww);
    } else if (n == 0) {
        b->eof = 1;
        ev_io_stop(EV_A_ & b->rw);
        LOG_TRACE("%s -> %s, EOF (buf:%zu/%zu)", b->from_label, b->to_label, b->sent, b->len);
        if (b->sent == b->len) {
            shutdown(b->to_fd, SHUT_WR);
            broker_done(b);
        }
    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        LOG_ERROR("%s -> %s, read error: %s", b->from_label, b->to_label, strerror(errno));
        shutdown(b->from_fd, SHUT_RD);
        shutdown(b->to_fd, SHUT_WR);
        ev_io_stop(EV_A_ & b->rw);
        ev_io_stop(EV_A_ & b->ww);
        broker_done(b);
    }
}

static broker_t *broker_new(conn_pair_t *p, int to_fd, int from_fd, const char *from_label,
                            const char *to_label)
{
    broker_t *b = (broker_t *)calloc(1, sizeof(*b));
    if (!b)
        return NULL;

    b->pair = p;
    b->to_fd = to_fd;
    b->from_fd = from_fd;
    strncpy(b->from_label, from_label, sizeof(b->from_label) - 1);
    strncpy(b->to_label, to_label, sizeof(b->to_label) - 1);

    set_nonblock(from_fd);
    ev_io_init(&b->rw, on_readable, from_fd, EV_READ);
    ev_io_init(&b->ww, on_writable, to_fd, EV_WRITE);
    b->rw.data = b->ww.data = b;

    ev_io_start(p->loop, &b->rw);
    return b;
}

// conn_pair_new - Create bidirectional proxy connection between client and backend
static void conn_pair_new(struct ev_loop *loop, int client_fd, int backend_fd,
                          const char *client_ip, int client_port, const char *backend_ip,
                          int backend_port)
{
    conn_pair_t *p = (conn_pair_t *)calloc(1, sizeof(*p));
    if (!p)
        return;

    p->loop = loop;
    p->client_fd = client_fd;
    p->backend_fd = backend_fd;
    p->start_time = ev_time();

    // [C] = client, [B] = backend
    char fwd_from_label[48], fwd_to_label[48];
    char bwd_from_label[48], bwd_to_label[48];

    snprintf(fwd_from_label, sizeof(fwd_from_label), "[C]%s:%d", client_ip, client_port);
    snprintf(fwd_to_label, sizeof(fwd_to_label), "[B]%s:%d", backend_ip, backend_port);
    snprintf(bwd_from_label, sizeof(bwd_from_label), "[B]%s:%d", backend_ip, backend_port);
    snprintf(bwd_to_label, sizeof(bwd_to_label), "[C]%s:%d", client_ip, client_port);

    set_nonblock(client_fd);
    set_nonblock(backend_fd);

    p->fwd = broker_new(p, backend_fd, client_fd, fwd_from_label, fwd_to_label);
    if (!p->fwd)
        goto cleanup;

    p->bwd = broker_new(p, client_fd, backend_fd, bwd_from_label, bwd_to_label);
    if (!p->bwd)
        goto cleanup;

    return;

cleanup:
    if (p->fwd)
        free(p->fwd);
    if (p->bwd)
        free(p->bwd);
    free(p);
    close(client_fd);
    close(backend_fd);
}

typedef struct {
    struct ev_loop *loop;
    int listen_fd;
    ev_io accept_w;
} server_t;

static void on_accept(struct ev_loop *loop, ev_io *w, int revents)
{
    server_t *s = (server_t *)w->data;
    (void)loop;
    (void)revents;

    struct sockaddr_in client_addr;
    socklen_t len = sizeof(client_addr);

    int client = accept(s->listen_fd, (struct sockaddr *)&client_addr, &len);
    if (client < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            LOG_ERROR("accept: %s", strerror(errno));
        return;
    }

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    int client_port = ntohs(client_addr.sin_port);
    LOG_INFO("Connection from %s:%d", client_ip, client_port);

    int backend = socket(AF_INET, SOCK_STREAM, 0);
    if (backend < 0) {
        LOG_ERROR("socket: %s", strerror(errno));
        close(client);
        return;
    }

    set_nonblock(client);
    set_nonblock(backend);

    struct sockaddr_in backend_addr;
    memset(&backend_addr, 0, sizeof(backend_addr));
    backend_addr.sin_family = AF_INET;
    backend_addr.sin_port = htons(g_cfg.backend_port);
    inet_pton(AF_INET, g_cfg.backend_addr, &backend_addr.sin_addr);

    if (connect(backend, (struct sockaddr *)&backend_addr, sizeof(backend_addr)) < 0 &&
        errno != EINPROGRESS) {
        LOG_ERROR("Connection to %s:%d failed", g_cfg.backend_addr, g_cfg.backend_port);
        close(client);
        close(backend);
        return;
    }

    LOG_INFO("Connected to %s:%d", g_cfg.backend_addr, g_cfg.backend_port);

    conn_pair_new(s->loop, client, backend, client_ip, client_port, g_cfg.backend_addr,
                  g_cfg.backend_port);
}

static server_t *server_new(struct ev_loop *loop, const char *addr, int port)
{
    server_t *s = (server_t *)calloc(1, sizeof(*s));
    if (!s)
        return NULL;

    s->loop = loop;

    s->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int reuse = 1;
    setsockopt(s->listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(port)};
    inet_pton(AF_INET, addr, &a.sin_addr);

    if (bind(s->listen_fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
        LOG_ERROR("bind %s:%d: %s", addr, port, strerror(errno));
        free(s);
        return NULL;
    }

    listen(s->listen_fd, 1024);
    set_nonblock(s->listen_fd);

    ev_io_init(&s->accept_w, on_accept, s->listen_fd, EV_READ);
    s->accept_w.data = s;
    ev_io_start(loop, &s->accept_w);

    LOG_INFO("Listening on %s:%d", addr, port);
    LOG_INFO("Forwarding to %s:%d", g_cfg.backend_addr, g_cfg.backend_port);

    return s;
}

static void server_free(server_t *s)
{
    if (s) {
        ev_io_stop(s->loop, &s->accept_w);
        close(s->listen_fd);
        free(s);
    }
}

#ifdef _WIN32
static void setup_signals(void) { /* Windows doesn't have SIGPIPE */ }
#else
static void setup_signals(void) { signal(SIGPIPE, SIG_IGN); }
#endif

static int parse_addr(const char *addr_str, char *host, size_t host_buf_len, int *port)
{
    const char *colon = strchr(addr_str, ':');
    if (!colon) {
        LOG_ERROR("Invalid address format: %s (expected host:port)", addr_str);
        return -1;
    }

    size_t len = colon - addr_str;
    if (len >= host_buf_len) {
        LOG_ERROR("Host too long");
        return -1;
    }

    strncpy(host, addr_str, len);
    host[len] = '\0';

    *port = atoi(colon + 1);
    if (*port <= 0 || *port > 65535) {
        LOG_ERROR("Invalid port: %s", colon + 1);
        return -1;
    }

    return 0;
}

#define VERSION "1.0.0"

static void print_version(const char *prog)
{
    printf("%s: Version %s\n", prog, VERSION);
    printf("Built with libev %d.%d\n", EV_VERSION_MAJOR, EV_VERSION_MINOR);
}

static void print_usage(const char *prog)
{
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("\nOptions:\n");
    printf("  -l, --listen-addr ADDR    Listen address (e.g., 0.0.0.0:8080)\n");
    printf("  -b, --backend-addr ADDR   Backend address (e.g., 127.0.0.1:8000)\n");
    printf("  -v, --verbose             Show connection details and traffic stats\n");
    printf("  -vv                       Show detailed read/write operations\n");
    printf("  -V, --version             Show version information\n");
    printf("  -h, --help                Show this help message\n");
    printf("\nExamples:\n");
    printf("  %s -l 0.0.0.0:8080 -b 127.0.0.1:8000\n", prog);
    printf("  %s --listen-addr 0.0.0.0:8080 --backend-addr backend.local:80 -vv\n", prog);
}

int main(int argc, char *argv[])
{
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
#endif

    static struct option long_options[] = {{"listen-addr", required_argument, 0, 'l'},
                                           {"backend-addr", required_argument, 0, 'b'},
                                           {"verbose", no_argument, 0, 'v'},
                                           {"version", no_argument, 0, 'V'},
                                           {"help", no_argument, 0, 'h'},
                                           {0, 0, 0, 0}};

    char listen_addr[24] = {0};
    int listen_port = 0;
    char backend_addr[24] = {0};
    int backend_port = 0;
    int opt;
    int ret = 0;

    while ((opt = getopt_long(argc, argv, "l:b:vVh", long_options, NULL)) != -1) {
        switch (opt) {
        case 'l':
            if (parse_addr(optarg, listen_addr, sizeof(listen_addr), &listen_port) < 0) {
                print_usage(argv[0]);
                ret = 1;
                goto cleanup;
            }
            break;
        case 'b':
            if (parse_addr(optarg, backend_addr, sizeof(backend_addr), &backend_port) < 0) {
                print_usage(argv[0]);
                ret = 1;
                goto cleanup;
            }
            break;
        case 'v':
            if (g_cfg.verbose < 2)
                g_cfg.verbose++;
            break;
        case 'V':
            print_version(argv[0]);
            ret = 0;
            goto cleanup;
        case 'h':
            print_usage(argv[0]);
            ret = 0;
            goto cleanup;
        default:
            print_usage(argv[0]);
            ret = 1;
            goto cleanup;
        }
    }

    if (listen_port == 0 || backend_port == 0) {
        LOG_ERROR("Both --listen-addr and --backend-addr are required");
        print_usage(argv[0]);
        ret = 1;
        goto cleanup;
    }

    g_cfg.listen_addr = listen_addr;
    g_cfg.listen_port = listen_port;
    g_cfg.backend_addr = backend_addr;
    g_cfg.backend_port = backend_port;

    setup_signals();

    struct ev_loop *loop = ev_default_loop(0);
    if (!loop) {
        LOG_ERROR("Failed to create event loop");
        ret = 1;
        goto cleanup;
    }

    server_t *s = server_new(loop, listen_addr, listen_port);
    if (!s) {
        ret = 1;
        goto cleanup;
    }

    ev_run(loop, 0);
    ev_loop_destroy(loop);
    server_free(s);

cleanup:
#ifdef _WIN32
    WSACleanup();
#endif
    return ret;
}
