#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <ev.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Version info defaults (for IDE static analysis)
#ifndef MYPROXY_VERSION
#define MYPROXY_VERSION "0.0.1"
#endif
#ifndef MYPROXY_BUILD_DATE
#define MYPROXY_BUILD_DATE "unknown"
#endif
#ifndef MYPROXY_COMMIT_HASH
#define MYPROXY_COMMIT_HASH "unknown"
#endif

#define BUF_SIZE (1024 * 32)
#define SPLICE_SIZE (1024 * 128)
#define PIPE_SIZE (1024 * 1024)

#if defined(__linux__)
#define USE_SPLICE
#endif

typedef struct {
    char listen_ip[24];
    int listen_port;
    char backend_ip[24];
    int backend_port;
} proxy_config_t;

typedef struct {
    int verbose;
} config_t;

static config_t g_cfg = {0};

#define LOG(level, fmt, ...)                                                                       \
    do {                                                                                           \
        if (g_cfg.verbose >= level)                                                                \
            printf(fmt "\n", ##__VA_ARGS__);                                                       \
    } while (0)
#define LOG_DEBUG(fmt, ...) LOG(1, fmt, ##__VA_ARGS__)
#define LOG_TRACE(fmt, ...) LOG(2, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)

static char *format_size(char *buf, size_t buf_len, size_t bytes)
{
    if (bytes < 1024)
        snprintf(buf, buf_len, "%zu B", bytes);
    else if (bytes < 1024 * 1024)
        snprintf(buf, buf_len, "%.2f KiB", bytes / 1024.0);
    else if (bytes < 1024 * 1024 * 1024)
        snprintf(buf, buf_len, "%.2f MiB", bytes / (1024.0 * 1024));
    else if (bytes < 1024ULL * 1024 * 1024 * 1024)
        snprintf(buf, buf_len, "%.2f GiB", bytes / (1024.0 * 1024 * 1024));
    else
        snprintf(buf, buf_len, "%.2f TiB", bytes / (1024.0 * 1024 * 1024 * 1024));
    return buf;
}

// =============================================================================
// Broker & Connection Pair
// =============================================================================

typedef struct conn_pair conn_pair_t;

typedef struct {
    conn_pair_t *pair;
    char from_label[32];
    char to_label[32];
    int to_fd;
    int from_fd;
    ev_io rw;
    ev_io ww;
    int eof;
#ifdef USE_SPLICE
    int pipe[2];
#else
    unsigned char buf[BUF_SIZE];
#endif
    size_t len;
    size_t sent;
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

static void set_nonblock(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK); }

#ifdef USE_SPLICE
static int broker_pipe_new(broker_t *b)
{
    b->pipe[0] = b->pipe[1] = -1;
    if (pipe(b->pipe) < 0)
        return -1;
#ifdef F_SETPIPE_SZ
    fcntl(b->pipe[0], F_SETPIPE_SZ, PIPE_SIZE);
    fcntl(b->pipe[1], F_SETPIPE_SZ, PIPE_SIZE);
#endif
    set_nonblock(b->pipe[0]);
    set_nonblock(b->pipe[1]);
    return 0;
}
#endif

static inline ssize_t broker_read(broker_t *b)
{
#ifdef USE_SPLICE
    return splice(b->from_fd, NULL, b->pipe[1], NULL, SPLICE_SIZE, SPLICE_F_NONBLOCK);
#else
    return read(b->from_fd, b->buf + b->len, BUF_SIZE - b->len);
#endif
}

static inline ssize_t broker_write(broker_t *b)
{
#ifdef USE_SPLICE
    return splice(b->pipe[0], NULL, b->to_fd, NULL, b->len - b->sent, SPLICE_F_NONBLOCK);
#else
    return write(b->to_fd, b->buf + b->sent, b->len - b->sent);
#endif
}

static inline int broker_buf_full(broker_t *b)
{
#ifdef USE_SPLICE
    (void)b;
    return 0;
#else
    return b->len == BUF_SIZE;
#endif
}

static void broker_done(broker_t *b)
{
    conn_pair_t *p = b->pair;
    ev_io_stop(p->loop, &b->rw);
    ev_io_stop(p->loop, &b->ww);

    if (++p->done != 2)
        return;

    double duration = ev_time() - p->start_time;

    char fwd_buf[32], fwd_rate_buf[32];
    char bwd_buf[32], bwd_rate_buf[32];
    LOG_DEBUG("[CLOSE#%d] %s -> %s (Duration: %.2fs)", b->pair->client_fd, p->fwd->from_label,
              p->fwd->to_label, duration);
    LOG_DEBUG(
        "[STATS#%d] FWD: %s (%s/s) | BWD: %s (%s/s)", b->pair->client_fd,
        format_size(fwd_buf, sizeof(fwd_buf), p->fwd->total_write),
        format_size(fwd_rate_buf, sizeof(fwd_rate_buf), (size_t)(p->fwd->total_write / duration)),
        format_size(bwd_buf, sizeof(bwd_buf), p->bwd->total_write),
        format_size(bwd_rate_buf, sizeof(bwd_rate_buf), (size_t)(p->bwd->total_write / duration)));

#ifdef USE_SPLICE
    close(p->fwd->pipe[0]);
    close(p->fwd->pipe[1]);
    close(p->bwd->pipe[0]);
    close(p->bwd->pipe[1]);
#endif
    close(p->client_fd);
    close(p->backend_fd);
    free(p->fwd);
    free(p->bwd);
    free(p);
}

static void broker_on_writable(struct ev_loop *loop, ev_io *w, int revents)
{
    (void)loop;
    (void)revents;

    broker_t *b = (broker_t *)w->data;

    if (b->sent == b->len) {
        b->len = b->sent = 0;
        ev_io_stop(loop, &b->ww);
        if (b->eof) {
            shutdown(b->to_fd, SHUT_WR);
            broker_done(b);
        } else if (!ev_is_active(&b->rw)) {
            ev_io_start(loop, &b->rw);
        }
        return;
    }

    ssize_t n = broker_write(b);
    if (n > 0) {
        b->total_write += n;
        b->sent += n;
        char n_buf[32], total_buf[32];
        LOG_TRACE("[DATA#%d] %s <- %s << WRITE : %10s | Total: %10s", b->pair->client_fd,
                  b->to_label, b->from_label, format_size(n_buf, sizeof(n_buf), n),
                  format_size(total_buf, sizeof(total_buf), b->total_write));

        if (b->sent == b->len) {
            b->len = b->sent = 0;
            ev_io_stop(loop, &b->ww);
            if (b->eof) {
                shutdown(b->to_fd, SHUT_WR);
                broker_done(b);
            } else if (!ev_is_active(&b->rw)) {
                ev_io_start(loop, &b->rw);
            }
        }
    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        LOG_ERROR("[ERROR#%d] %s <- %s WRITE error: %s", b->pair->client_fd, b->to_label,
                  b->from_label, strerror(errno));
        shutdown(b->from_fd, SHUT_RD);
        shutdown(b->to_fd, SHUT_WR);
        ev_io_stop(loop, &b->ww);
        ev_io_stop(loop, &b->rw);
        broker_done(b);
    }
}

static void broker_on_readable(struct ev_loop *loop, ev_io *w, int revents)
{
    (void)loop;
    (void)revents;

    broker_t *b = (broker_t *)w->data;

    // Prevent read returning 0 and being mistaken for EOF when buffer is full
    if (broker_buf_full(b)) {
        ev_io_stop(loop, &b->rw);
        return;
    }

    ssize_t n = broker_read(b);
    if (n > 0) {
        b->total_read += n;
        b->len += n;
        char n_buf[32], total_buf[32];
        LOG_TRACE("[DATA#%d] %s -> %s >> READ  : %10s | Total: %10s", b->pair->client_fd,
                  b->from_label, b->to_label, format_size(n_buf, sizeof(n_buf), n),
                  format_size(total_buf, sizeof(total_buf), b->total_read));

        if (broker_buf_full(b))
            ev_io_stop(loop, &b->rw);
        if (!ev_is_active(&b->ww))
            ev_io_start(loop, &b->ww);
    } else if (n == 0) {
        b->eof = 1;
        ev_io_stop(loop, &b->rw);
        LOG_TRACE("[DATA#%d] %s -> %s EOF (sent:%zu len:%zu)", b->pair->client_fd, b->from_label,
                  b->to_label, b->sent, b->len);
        if (b->sent == b->len) {
            shutdown(b->to_fd, SHUT_WR);
            broker_done(b);
        }
    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        LOG_ERROR("[ERROR#%d] %s -> %s READ error: %s", b->pair->client_fd, b->from_label,
                  b->to_label, strerror(errno));
        shutdown(b->from_fd, SHUT_RD);
        shutdown(b->to_fd, SHUT_WR);
        ev_io_stop(loop, &b->rw);
        ev_io_stop(loop, &b->ww);
        broker_done(b);
    }
}

static broker_t *broker_new(conn_pair_t *p, int to_fd, int from_fd, int conn_id,
                            const char *from_label, const char *to_label)
{
    broker_t *b = (broker_t *)calloc(1, sizeof(*b));
    if (!b)
        return NULL;

    b->pair = p;
    b->pair->client_fd = conn_id;
    b->to_fd = to_fd;
    b->from_fd = from_fd;
    strncpy(b->from_label, from_label, sizeof(b->from_label) - 1);
    strncpy(b->to_label, to_label, sizeof(b->to_label) - 1);

#ifdef USE_SPLICE
    if (broker_pipe_new(b) < 0) {
        LOG_ERROR("broker_new: pipe failed: %s", strerror(errno));
        free(b);
        return NULL;
    }
#endif

    set_nonblock(from_fd);
    ev_io_init(&b->rw, broker_on_readable, from_fd, EV_READ);
    ev_io_init(&b->ww, broker_on_writable, to_fd, EV_WRITE);
    b->rw.data = b->ww.data = b;

    ev_io_start(p->loop, &b->rw);
    return b;
}

// conn_pair_new - Create bidirectional proxy connection between client and backend
static void conn_pair_new(struct ev_loop *loop, int client_fd, const char *client_ip,
                          int client_port, const char *backend_ip, int backend_port)
{
    int backend_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (backend_fd < 0) {
        LOG_ERROR("socket: %s", strerror(errno));
        close(client_fd);
        return;
    }

    set_nonblock(client_fd);
    set_nonblock(backend_fd);

    struct sockaddr_in backend_addr;
    memset(&backend_addr, 0, sizeof(backend_addr));
    backend_addr.sin_family = AF_INET;
    backend_addr.sin_port = htons(backend_port);
    inet_pton(AF_INET, backend_ip, &backend_addr.sin_addr);

    if (connect(backend_fd, (struct sockaddr *)&backend_addr, sizeof(backend_addr)) < 0 &&
        errno != EINPROGRESS) {
        LOG_ERROR("Connection to %s:%d failed", backend_ip, backend_port);
        close(client_fd);
        close(backend_fd);
        return;
    }

    conn_pair_t *p = (conn_pair_t *)calloc(1, sizeof(*p));
    if (!p) {
        close(client_fd);
        close(backend_fd);
        return;
    }

    p->loop = loop;
    p->client_fd = client_fd;
    p->backend_fd = backend_fd;
    p->start_time = ev_time();

    char client_label[32], backend_label[32];
    snprintf(client_label, sizeof(client_label), "%s:%d", client_ip, client_port);
    snprintf(backend_label, sizeof(backend_label), "%s:%d", backend_ip, backend_port);

    // Log connection OPEN
    LOG_DEBUG("[OPEN#%d] %s -> %s", client_fd, client_label, backend_label);

    p->fwd = broker_new(p, backend_fd, client_fd, client_fd, client_label, backend_label);
    if (!p->fwd)
        goto cleanup;

    p->bwd = broker_new(p, client_fd, backend_fd, client_fd, backend_label, client_label);
    if (!p->bwd)
        goto cleanup;

    return;

cleanup:
#ifdef USE_SPLICE
    if (p->fwd) {
        close(p->fwd->pipe[0]);
        close(p->fwd->pipe[1]);
    }
    if (p->bwd) {
        close(p->bwd->pipe[0]);
        close(p->bwd->pipe[1]);
    }
#endif
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
    char backend_ip[24];
    int backend_port;
} server_t;

static void server_on_accept(struct ev_loop *loop, ev_io *w, int revents)
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

    conn_pair_new(s->loop, client, client_ip, client_port, s->backend_ip, s->backend_port);
}

static server_t *server_new(struct ev_loop *loop, const char *listen_ip, int listen_port,
                            const char *backend_ip, int backend_port)
{
    server_t *s = (server_t *)calloc(1, sizeof(*s));
    if (!s)
        return NULL;

    s->loop = loop;
    strncpy(s->backend_ip, backend_ip, sizeof(s->backend_ip) - 1);
    s->backend_port = backend_port;

    s->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int reuse = 1;
    setsockopt(s->listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(listen_port)};
    inet_pton(AF_INET, listen_ip, &a.sin_addr);

    if (bind(s->listen_fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
        LOG_ERROR("bind %s:%d: %s", listen_ip, listen_port, strerror(errno));
        free(s);
        return NULL;
    }

    listen(s->listen_fd, 1024);
    set_nonblock(s->listen_fd);

    ev_io_init(&s->accept_w, server_on_accept, s->listen_fd, EV_READ);
    s->accept_w.data = s;
    ev_io_start(loop, &s->accept_w);

    LOG_DEBUG("[PROXY#%d] %s:%d -> %s:%d", s->listen_fd, listen_ip, listen_port, backend_ip,
              backend_port);

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

static void setup_signals(void) { signal(SIGPIPE, SIG_IGN); }

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

// =============================================================================
// Config File Parser
// =============================================================================
// Format: verbose=0 | listen,backend (one per line)

#define MAX_CONFIGS 64

static proxy_config_t g_configs[MAX_CONFIGS];
static int g_config_count = 0;

// trim_whitespace - Trim leading and trailing whitespace from a string (in-place)
static void trim_whitespace(char *s)
{
    if (!s)
        return;

    // Trim leading whitespace
    char *start = s;
    while (*start == ' ' || *start == '\t')
        start++;

    // Trim trailing whitespace
    char *end = s + strlen(s) - 1;
    while (end >= start && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r'))
        *end-- = '\0';

    // Shift string if leading whitespace was trimmed
    if (start != s) {
        memmove(s, start, strlen(start) + 1);
    }
}

// parse_global_option - Parse global config option (key=value)
// Supported options: verbose (0=quiet, 1=info, 2=debug)
// Returns: 0 = success, -1 = error, 1 = not a global option
static int parse_global_option(char *line, int line_num)
{
    char *eq = strchr(line, '=');
    if (!eq)
        return 1; // Not a key=value pair

    // Check if it has comma (proxy config), if so, not a global option
    if (strchr(line, ','))
        return 1;

    *eq = '\0';
    char *key = line;
    char *val = eq + 1;

    trim_whitespace(key);
    trim_whitespace(val);

    // Parse verbose option
    if (strcmp(key, "verbose") == 0) {
        g_cfg.verbose = atoi(val);
        if (g_cfg.verbose < 0 || g_cfg.verbose > 2) {
            LOG_ERROR("Invalid verbose value at line %d: %s (must be 0-2)", line_num, val);
            return -1;
        }
    } else {
        LOG_ERROR("Unknown config option at line %d: %s", line_num, key);
        return -1;
    }

    return 0;
}

// parse_proxy_config_line - Parse a single proxy config line (listen,backend)
// Format: listen-addr,backend-addr (e.g., "0.0.0.0:8080,127.0.0.1:8000")
// Returns: 0 = success, -1 = error
static int parse_proxy_config_line(char *line, int line_num, int index)
{
    char *comma = strchr(line, ',');
    if (!comma) {
        LOG_ERROR("Invalid config at line %d: missing comma separator", line_num);
        return -1;
    }

    *comma = '\0';
    char *listen_str = line;
    char *backend_str = comma + 1;

    trim_whitespace(listen_str);
    trim_whitespace(backend_str);

    // Parse listen address
    char listen_ip[24];
    int listen_port;
    if (parse_addr(listen_str, listen_ip, sizeof(listen_ip), &listen_port) < 0) {
        LOG_ERROR("Invalid listen address at line %d: %s", line_num, listen_str);
        return -1;
    }

    // Parse backend address
    char backend_ip[24];
    int backend_port;
    if (parse_addr(backend_str, backend_ip, sizeof(backend_ip), &backend_port) < 0) {
        LOG_ERROR("Invalid backend address at line %d: %s", line_num, backend_str);
        return -1;
    }

    // Store config
    snprintf(g_configs[index].listen_ip, sizeof(g_configs[index].listen_ip), "%s", listen_ip);
    g_configs[index].listen_port = listen_port;
    snprintf(g_configs[index].backend_ip, sizeof(g_configs[index].backend_ip), "%s", backend_ip);
    g_configs[index].backend_port = backend_port;

    return 0;
}

// parse_config_file - Parse config file with global options and proxy configs
// The file is processed line by line:
//   1. Empty lines and comments (#) are skipped
//   2. Lines with '=' are parsed as global options (verbose, etc.)
//   3. Lines with ',' are parsed as proxy configs (listen,backend)
// Returns: 0 = success, -1 = error
static int parse_config_file(const char *filepath)
{
    FILE *f = fopen(filepath, "r");
    if (!f) {
        LOG_ERROR("Failed to open config file: %s (%s)", filepath, strerror(errno));
        return -1;
    }

    char line[512];
    int line_num = 0;
    int count = 0;

    while (fgets(line, sizeof(line), f) && count < MAX_CONFIGS) {
        line_num++;

        // Trim the line (removes newline and whitespace)
        trim_whitespace(line);

        // Skip empty lines and comments
        if (line[0] == '\0' || line[0] == '#')
            continue;

        // Try to parse as global option first
        int ret = parse_global_option(line, line_num);
        if (ret < 0) {
            fclose(f);
            return -1;
        }
        if (ret == 0)
            continue; // Global option parsed successfully

        // Parse as proxy config
        if (parse_proxy_config_line(line, line_num, count) < 0) {
            fclose(f);
            return -1;
        }
        count++;
    }

    fclose(f);

    if (count == 0) {
        LOG_ERROR("Config file has no proxy configs: %s", filepath);
        return -1;
    }

    // Check for duplicate listen addresses
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            if (g_configs[i].listen_port == g_configs[j].listen_port &&
                strcmp(g_configs[i].listen_ip, g_configs[j].listen_ip) == 0) {
                LOG_ERROR("Duplicate listen address: %s:%d", g_configs[i].listen_ip,
                          g_configs[i].listen_port);
                return -1;
            }
        }
    }

    g_config_count = count;
    return 0;
}

static void print_version(const char *prog)
{
    printf("%s %s (%s %s)\n", prog, MYPROXY_VERSION, MYPROXY_COMMIT_HASH, MYPROXY_BUILD_DATE);
}

static void print_usage(const char *prog)
{
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("\nOptions:\n");
    printf("  -c, --config FILE         Config file with proxy configs\n");
    printf("  -l, --listen-addr ADDR    Listen address (e.g., 0.0.0.0:8080)\n");
    printf("  -b, --backend-addr ADDR   Backend address (e.g., 127.0.0.1:8000)\n");
    printf("  -v, --verbose             Show connection details and traffic stats\n");
    printf("  -vv                       Show detailed read/write operations\n");
    printf("  -V, --version             Show version information\n");
    printf("  -h, --help                Show this help message\n");
    printf("\nNotes:\n");
    printf("  --config is mutually exclusive with --listen-addr/--backend-addr\n");
    printf("\nExamples:\n");
    printf("  %s -l 0.0.0.0:8080 -b 127.0.0.1:8000\n", prog);
    printf("  %s --listen-addr 0.0.0.0:8080 --backend-addr backend.local:80 -vv\n", prog);
    printf("  %s -c /etc/myproxy.conf\n", prog);
    printf("\nConfig file format:\n");
    printf("  # Global options (key=value)\n");
    printf("  verbose=0               # 0=quiet, 1=info, 2=debug\n");
    printf("  # Proxy configs (listen,backend)\n");
    printf("  0.0.0.0:8080,127.0.0.1:8000\n");
    printf("  0.0.0.0:8081,127.0.0.1:8001\n");
}

int main(int argc, char *argv[])
{
    static struct option long_options[] = {{"config", required_argument, 0, 'c'},
                                           {"listen-addr", required_argument, 0, 'l'},
                                           {"backend-addr", required_argument, 0, 'b'},
                                           {"verbose", no_argument, 0, 'v'},
                                           {"version", no_argument, 0, 'V'},
                                           {"help", no_argument, 0, 'h'},
                                           {0, 0, 0, 0}};

    char listen_ip[24] = {0};
    int listen_port = 0;
    char backend_ip[24] = {0};
    int backend_port = 0;
    char *config_file = NULL;
    int opt;
    int ret = 0;
    int has_cli_config = 0;

    while ((opt = getopt_long(argc, argv, "c:l:b:vVh", long_options, NULL)) != -1) {
        switch (opt) {
        case 'c':
            config_file = optarg;
            break;
        case 'l':
            if (parse_addr(optarg, listen_ip, sizeof(listen_ip), &listen_port) < 0) {
                print_usage(argv[0]);
                ret = 1;
                goto cleanup;
            }
            has_cli_config = 1;
            break;
        case 'b':
            if (parse_addr(optarg, backend_ip, sizeof(backend_ip), &backend_port) < 0) {
                print_usage(argv[0]);
                ret = 1;
                goto cleanup;
            }
            has_cli_config = 1;
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

    // Check mutual exclusivity
    if (config_file && has_cli_config) {
        LOG_ERROR("--config is mutually exclusive with --listen-addr/--backend-addr");
        print_usage(argv[0]);
        ret = 1;
        goto cleanup;
    }

    // Load configs from file or CLI
    if (config_file) {
        if (parse_config_file(config_file) < 0) {
            ret = 1;
            goto cleanup;
        }
    } else if (has_cli_config) {
        if (listen_port == 0 || backend_port == 0) {
            LOG_ERROR("Both --listen-addr and --backend-addr are required");
            print_usage(argv[0]);
            ret = 1;
            goto cleanup;
        }
        // Create single config from CLI
        strncpy(g_configs[0].listen_ip, listen_ip, sizeof(g_configs[0].listen_ip) - 1);
        g_configs[0].listen_ip[sizeof(g_configs[0].listen_ip) - 1] = '\0';
        g_configs[0].listen_port = listen_port;
        strncpy(g_configs[0].backend_ip, backend_ip, sizeof(g_configs[0].backend_ip) - 1);
        g_configs[0].backend_ip[sizeof(g_configs[0].backend_ip) - 1] = '\0';
        g_configs[0].backend_port = backend_port;
        g_config_count = 1;
    } else {
        LOG_ERROR("Either --config or both --listen-addr and --backend-addr are required");
        print_usage(argv[0]);
        ret = 1;
        goto cleanup;
    }

    setup_signals();

    struct ev_loop *loop = ev_default_loop(0);
    if (!loop) {
        LOG_ERROR("Failed to create event loop");
        ret = 1;
        goto cleanup;
    }

    // Create servers for all configs
    server_t *servers[MAX_CONFIGS] = {NULL};
    int server_count = 0;

    for (int i = 0; i < g_config_count; i++) {
        server_t *s = server_new(loop, g_configs[i].listen_ip, g_configs[i].listen_port,
                                 g_configs[i].backend_ip, g_configs[i].backend_port);
        if (!s) {
            ret = 1;
            goto cleanup_servers;
        }
        servers[server_count++] = s;
    }

    ev_run(loop, 0);
    ev_loop_destroy(loop);

cleanup_servers:
    for (int i = 0; i < server_count; i++) {
        server_free(servers[i]);
    }

cleanup:
    return ret;
}
