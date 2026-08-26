/*
 * net.c
 *
 * 贪吃蛇客户端 —— 网络模块实现（纯二进制帧协议）
 *
 * - 后台线程负责建立连接（带超时）并持续 recv；
 * - 接收到的完整二进制帧放入线程安全的消息队列；
 * - 主线程用 net_poll_msg() 取帧、net_send_msg() 发帧、net_state() 查状态。
 *
 * 帧格式：1 字节类型 + 2 字节负载长度（大端）+ 负载（见 protocol.h）。
 *
 * 平台：Linux（arm-linux-gcc 交叉编译，板子上为 Linux 3.4.39）
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <fcntl.h>

#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "net.h"
#include "protocol.h"

/* ----------------------------------------------------------------- */
/* 内部常量                                                           */
/* ----------------------------------------------------------------- */
#define NET_QUEUE_DEPTH 8           /* 消息队列深度（STATE 帧较大，不宜过多） */
#define NET_ACC_MAX     (PROTO_HEADER_LEN + PROTO_MAX_PAYLOAD)   /* 接收累积缓冲 */
#define NET_HOST_MAX    64

/* ----------------------------------------------------------------- */
/* 模块状态                                                           */
/* ----------------------------------------------------------------- */
typedef struct {
    uint8_t  type;
    uint16_t len;
    uint8_t  data[PROTO_MAX_PAYLOAD];
} net_msg_t;

static volatile int      g_fd = -1;
static volatile net_state_t g_state = NET_DISCONNECTED;
static pthread_t         g_thread;
static volatile int      g_thread_running = 0;

static pthread_mutex_t   g_queue_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t   g_state_lock = PTHREAD_MUTEX_INITIALIZER;

static net_msg_t g_q[NET_QUEUE_DEPTH];
static int  g_q_head = 0;
static int  g_q_tail = 0;
static int  g_q_count = 0;

static char g_host[NET_HOST_MAX];
static int  g_port = 0;
static int  g_timeout_ms = 3000;

static char g_last_error[128] = "ok";

/* ----------------------------------------------------------------- */
/* 小工具                                                             */
/* ----------------------------------------------------------------- */
static void set_state(net_state_t s)
{
    pthread_mutex_lock(&g_state_lock);
    g_state = s;
    pthread_mutex_unlock(&g_state_lock);
}

static void set_error(const char *msg)
{
    strncpy(g_last_error, msg, sizeof(g_last_error) - 1);
    g_last_error[sizeof(g_last_error) - 1] = '\0';
}

/* ----------------------------------------------------------------- */
/* 消息队列                                                           */
/* ----------------------------------------------------------------- */
static void queue_push(uint8_t type, const uint8_t *data, uint16_t len)
{
    pthread_mutex_lock(&g_queue_lock);
    if (g_q_count >= NET_QUEUE_DEPTH) {
        /* 满了：丢弃最旧的一帧，腾出位置 */
        g_q_head = (g_q_head + 1) % NET_QUEUE_DEPTH;
        g_q_count--;
    }
    g_q[g_q_tail].type = type;
    g_q[g_q_tail].len = len;
    if (len > 0 && data) memcpy(g_q[g_q_tail].data, data, len);
    g_q_tail = (g_q_tail + 1) % NET_QUEUE_DEPTH;
    g_q_count++;
    pthread_mutex_unlock(&g_queue_lock);
}

static int queue_pop(net_msg_t *out)
{
    int got = 0;
    pthread_mutex_lock(&g_queue_lock);
    if (g_q_count > 0) {
        *out = g_q[g_q_head];
        g_q_head = (g_q_head + 1) % NET_QUEUE_DEPTH;
        g_q_count--;
        got = 1;
    }
    pthread_mutex_unlock(&g_queue_lock);
    return got;
}

/* ----------------------------------------------------------------- */
/* 建立连接（后台线程内调用，带超时）                                 */
/* ----------------------------------------------------------------- */
static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int set_blocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
}

static int connect_timeout(const char *host, int port, int timeout_ms)
{
    struct addrinfo hints, *res = NULL, *rp;
    char portstr[16];
    int fd = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;      /* 局域网，强制 IPv4 */
    hints.ai_socktype = SOCK_STREAM;
    snprintf(portstr, sizeof(portstr), "%d", port);

    if (getaddrinfo(host, portstr, &hints, &res) != 0) {
        set_error("无法解析服务器地址");
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        if (set_nonblocking(fd) < 0) { close(fd); fd = -1; continue; }

        int rc = connect(fd, rp->ai_addr, rp->ai_addrlen);
        if (rc == 0) { set_blocking(fd); break; }

        if (errno == EINPROGRESS) {
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(fd, &wfds);
            struct timeval tv;
            tv.tv_sec  = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            int sr = select(fd + 1, NULL, &wfds, NULL, &tv);
            if (sr > 0 && FD_ISSET(fd, &wfds)) {
                int soerr = 0;
                socklen_t l = sizeof(soerr);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &l);
                if (soerr == 0) { set_blocking(fd); break; }
            }
        }
        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    if (fd < 0) set_error("连接服务器失败");
    return fd;
}

/* ----------------------------------------------------------------- */
/* 接收线程                                                           */
/* ----------------------------------------------------------------- */
static void *recv_thread(void *arg)
{
    (void)arg;

    int fd = connect_timeout(g_host, g_port, g_timeout_ms);
    if (fd < 0) {
        set_state(NET_DISCONNECTED);
        queue_push(MSG_NETCLOSED, NULL, 0);
        g_thread_running = 0;
        return NULL;
    }

    g_fd = fd;
    set_blocking(fd);           /* 确保接收在阻塞模式，避免 EAGAIN 误断 */
    set_state(NET_CONNECTED);

    uint8_t acc[NET_ACC_MAX];
    int  acclen = 0;

    while (1) {
        char buf[1024];
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            break;              /* 出错，断开 */
        }
        if (n == 0) break;      /* 对端关闭 */

        if (acclen + n > (int)sizeof(acc)) acclen = 0;   /* 溢出保护 */

        memcpy(acc + acclen, buf, (size_t)n);
        acclen += (int)n;

        /* 切分完整二进制帧 */
        int pos = 0;
        while (acclen - pos >= PROTO_HEADER_LEN) {
            uint8_t type = acc[pos];
            uint16_t plen = (uint16_t)(((uint16_t)acc[pos + 1] << 8) | acc[pos + 2]);
            if (PROTO_HEADER_LEN + plen > NET_ACC_MAX) {   /* 非法帧，丢弃缓冲 */
                acclen = 0;
                pos = 0;
                break;
            }
            if (acclen - pos < PROTO_HEADER_LEN + plen) break;
            queue_push(type, acc + pos + PROTO_HEADER_LEN, plen);
            pos += PROTO_HEADER_LEN + plen;
        }
        if (pos > 0) {
            acclen -= pos;
            memmove(acc, acc + pos, (size_t)acclen);
        }
    }

    close(fd);
    g_fd = -1;
    set_state(NET_DISCONNECTED);
    queue_push(MSG_NETCLOSED, NULL, 0);
    g_thread_running = 0;
    return NULL;
}

/* ----------------------------------------------------------------- */
/* 对外接口                                                           */
/* ----------------------------------------------------------------- */
int net_init(void)
{
    return 0;
}

int net_connect(const net_cfg_t *cfg)
{
    /* 若已有连接/正在连接，先断开 */
    if (g_thread_running) net_close();

    strncpy(g_host, cfg->host ? cfg->host : "127.0.0.1", NET_HOST_MAX - 1);
    g_host[NET_HOST_MAX - 1] = '\0';
    g_port       = cfg->port;
    g_timeout_ms = (cfg->timeout_ms > 0) ? cfg->timeout_ms : 3000;

    set_state(NET_CONNECTING);
    g_thread_running = 1;
    if (pthread_create(&g_thread, NULL, recv_thread, NULL) != 0) {
        set_error("创建线程失败");
        g_thread_running = 0;
        set_state(NET_DISCONNECTED);
        return -1;
    }
    return 0;
}

void net_close(void)
{
    if (!g_thread_running) return;
    int fd = g_fd;
    if (fd >= 0) {
        shutdown(fd, SHUT_RDWR);   /* 唤醒阻塞中的 recv */
    }
    pthread_join(g_thread, NULL);
    g_thread_running = 0;
    g_fd = -1;
    set_state(NET_DISCONNECTED);
}

int net_send_msg(uint8_t type, const void *payload, uint16_t plen)
{
    if (g_state != NET_CONNECTED || g_fd < 0) return -1;
    if (plen > PROTO_MAX_PAYLOAD) return -1;

    uint8_t hdr[PROTO_HEADER_LEN];
    hdr[0] = type;
    hdr[1] = (uint8_t)(plen >> 8);
    hdr[2] = (uint8_t)(plen & 0xff);

    ssize_t written = 0;
    while ((size_t)written < sizeof(hdr)) {
        ssize_t n = send(g_fd, hdr + written, sizeof(hdr) - (size_t)written, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        written += n;
    }
    if (plen > 0 && payload) {
        written = 0;
        while ((size_t)written < plen) {
            ssize_t n = send(g_fd, (const char *)payload + written, plen - (size_t)written, MSG_NOSIGNAL);
            if (n < 0) {
                if (errno == EINTR) continue;
                return -1;
            }
            written += n;
        }
    }
    return 0;
}

int net_poll_msg(uint8_t *type, uint8_t *out, int outlen, int *plen_out)
{
    /* 先取出队列中的帧；若没有任何帧且已断开，返回 -1 */
    net_msg_t m;
    if (queue_pop(&m)) {
        if (type) *type = m.type;
        if (plen_out) *plen_out = m.len;
        if (out && outlen > 0) {
            int n = m.len;
            if (n > outlen - 1) n = outlen - 1;
            if (n > 0) memcpy(out, m.data, (size_t)n);
            out[n] = '\0';
        }
        return 1;
    }
    if (net_state() == NET_DISCONNECTED) {
        if (type) *type = MSG_NETCLOSED;
        return -1;
    }
    return 0;
}

net_state_t net_state(void)
{
    net_state_t s;
    pthread_mutex_lock(&g_state_lock);
    s = g_state;
    pthread_mutex_unlock(&g_state_lock);
    return s;
}

const char *net_last_error(void)
{
    return g_last_error;
}
