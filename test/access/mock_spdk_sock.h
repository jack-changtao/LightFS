#ifndef MOCK_SPDK_SOCK_H
#define MOCK_SPDK_SOCK_H

#include <spdk/sock.h>
#include <spdk/queue.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *mock_recv_data;
static size_t mock_recv_len;
static size_t mock_recv_offset;
static struct iovec *mock_written_iov;
static int mock_written_iovcnt;
static bool mock_sock_closed;
static int mock_accept_count;

static void mock_sock_reset(void) {
    mock_recv_data = NULL;
    mock_recv_len = 0;
    mock_recv_offset = 0;
    mock_written_iov = NULL;
    mock_written_iovcnt = 0;
    mock_sock_closed = false;
    mock_accept_count = 0;
}

static void mock_sock_set_recv(const uint8_t *data, size_t len) {
    mock_recv_data = (uint8_t *)data;
    mock_recv_len = len;
    mock_recv_offset = 0;
    mock_sock_closed = false;
}

struct spdk_sock *spdk_sock_listen_ext(const char *ip, int port,
                                        const char *impl, struct spdk_sock_opts *opts) {
    (void)ip; (void)port; (void)impl; (void)opts;
    return (struct spdk_sock *)0x1;
}

struct spdk_sock *spdk_sock_accept(struct spdk_sock *sock) {
    (void)sock;
    mock_accept_count++;
    return (mock_accept_count == 1) ? (struct spdk_sock *)0x2 : NULL;
}

ssize_t spdk_sock_recv(struct spdk_sock *sock, void *buf, size_t len) {
    (void)sock;
    size_t avail = mock_recv_len - mock_recv_offset;
    size_t copy = avail < len ? avail : len;
    memcpy(buf, mock_recv_data + mock_recv_offset, copy);
    mock_recv_offset += copy;
    return (ssize_t)copy;
}

void spdk_sock_writev_async(struct spdk_sock *sock, struct spdk_sock_request *req) {
    (void)sock;
    mock_written_iov = SPDK_SOCK_REQUEST_IOV(req, 0);
    mock_written_iovcnt = req->iovcnt;
    if (req->cb_fn) {
        req->cb_fn(req->cb_arg, 0);
        /* cb_fn is responsible for freeing req */
    } else {
        free(req);
    }
}

int spdk_sock_close(struct spdk_sock **sock) {
    mock_sock_closed = true;
    *sock = NULL;
    return 0;
}

void spdk_sock_get_default_opts(struct spdk_sock_opts *opts) {
    memset(opts, 0, sizeof(*opts));
    opts->opts_size = sizeof(*opts);
}

struct spdk_sock_group *spdk_sock_group_create(void *ctx) {
    (void)ctx;
    return (struct spdk_sock_group *)0x3;
}

int spdk_sock_group_add_sock(struct spdk_sock_group *group, struct spdk_sock *sock,
                              spdk_sock_cb cb, void *arg) {
    (void)group; (void)sock; (void)cb; (void)arg;
    return 0;
}

int spdk_sock_group_remove_sock(struct spdk_sock_group *group, struct spdk_sock *sock) {
    (void)group; (void)sock;
    return 0;
}

int spdk_sock_group_poll(struct spdk_sock_group *group) {
    (void)group;
    return 0;
}

int spdk_sock_group_close(struct spdk_sock_group **group) {
    *group = NULL;
    return 0;
}

#endif /* MOCK_SPDK_SOCK_H */
