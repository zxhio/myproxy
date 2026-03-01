/* Simple echo server for testing myproxy */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <ev.h>

#define BUF_SIZE (1024 * 32)

static void set_nonblock(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK); }

typedef struct {
    int fd;
    char buf[BUF_SIZE];
    size_t len;
    size_t sent;
    ev_io rw;
    ev_io ww;
} conn_t;

static void conn_on_io(struct ev_loop *loop, ev_io *w, int revents)
{
    (void)revents;
    conn_t *c = (conn_t *)w->data;

    if (revents & EV_WRITE) {
        ssize_t n = write(c->fd, c->buf + c->sent, c->len - c->sent);
        if (n > 0) {
            c->sent += n;
            if (c->sent == c->len) {
                c->len = c->sent = 0;
                ev_io_stop(loop, &c->ww);
            }
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            goto close;
        }
    }

    if (revents & EV_READ) {
        ssize_t n = read(c->fd, c->buf + c->len, BUF_SIZE - c->len);
        if (n > 0) {
            c->len += n;
            if (!ev_is_active(&c->ww))
                ev_io_start(loop, &c->ww);
        } else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
            goto close;
        }
    }

    return;

close:
    ev_io_stop(loop, &c->rw);
    ev_io_stop(loop, &c->ww);
    close(c->fd);
    free(c);
}

static void server_on_accept(struct ev_loop *loop, ev_io *w, int revents)
{
    (void)revents;
    int client = accept(w->fd, NULL, NULL);
    if (client < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            perror("accept");
        return;
    }

    set_nonblock(client);
    conn_t *c = calloc(1, sizeof(*c));
    if (!c) {
        close(client);
        return;
    }

    c->fd = client;
    ev_io_init(&c->rw, conn_on_io, client, EV_READ);
    ev_io_init(&c->ww, conn_on_io, client, EV_WRITE);
    c->rw.data = c->ww.data = c;
    ev_io_start(loop, &c->rw);
}

int main(int argc, char *argv[])
{
    const char *addr = "0.0.0.0";
    int port = 8000;

    if (argc >= 2)
        addr = argv[1];
    if (argc >= 3)
        port = atoi(argv[2]);

    signal(SIGPIPE, SIG_IGN);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(port)};
    inet_pton(AF_INET, addr, &a.sin_addr);

    if (bind(listen_fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
        perror("bind");
        return 1;
    }

    listen(listen_fd, 1024);
    set_nonblock(listen_fd);

    struct ev_loop *loop = ev_default_loop(0);
    ev_io accept_w;
    ev_io_init(&accept_w, server_on_accept, listen_fd, EV_READ);
    ev_io_start(loop, &accept_w);

    printf("Echo server listening on %s:%d\n", addr, port);
    ev_run(loop, 0);

    return 0;
}
