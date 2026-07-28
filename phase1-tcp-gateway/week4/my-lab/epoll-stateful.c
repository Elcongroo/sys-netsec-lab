/**
 * epoll-stateful.c — 任务三：有限状态机 + epoll
 *
 * 设计意图：真实网络编程中，你不能假设"一发一收"。
 * 你需要给每个客户端绑定一个"状态"：
 *
 *   struct conn:
 *     - fd            → 这个客户端的文件描述符
 *     - state          → 当前在做什么（READING / WRITING / CLOSED）
 *     - recv_buf       → 接收缓冲区（数据可能分多次到达）
 *     - recv_len       → 已接收的字节数
 *     - expect_len     → 期望读多少字节（0 = 读到 \n 为止）
 *     - send_buf       → 发送缓冲区（可能一次写不完）
 *     - send_len       → 还没写完的字节数
 *
 * 为什么需要状态机？
 *   非阻塞模式下，read/write 都是"能读多少读多少"。
 *   你可能一次 read 只读到半条消息——剩下一半要等下次 epoll_wait 唤醒。
 *   如果没有"状态"，下次唤醒时你不知道"上次读到哪里"。
 *
 * 编译：gcc -Wall -g -O0 -o epoll-stateful epoll-stateful.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT        8080
#define MAX_EVENTS  64
#define MAX_CLIENTS 1024
#define BUF_SIZE    4096

/* ---- 连接状态 ---- */
typedef enum {
    STATE_READ,    /* 等待读数据 */
    STATE_WRITE,   /* 等待写完数据 */
    STATE_CLOSE    /* 待清理 */
} conn_state_t;

/* ---- 每个客户端的上下文 ---- */
typedef struct {
    int    fd;
    conn_state_t state;
    char   recv_buf[BUF_SIZE];
    size_t recv_len;
    char   send_buf[BUF_SIZE];
    size_t send_len;
    size_t send_done;
} conn_t;

static conn_t* g_conns[MAX_CLIENTS];  /* 按 fd 索引 */
static int     keep_running = 1;

static void sigint_handler(int sig) { (void)sig; keep_running = 0; }
static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/*
 * ================================================================
 *  do_read：非阻塞地尽量读，读到 \n 或 缓冲区满为止
 * ================================================================
 *
 * 四种返回情况：
 *   1. 读到了 \n  → 转到 STATE_WRITE
 *   2. 缓冲区满了  → 转到 STATE_WRITE（防止无限读）
 *   3. read=0      → 对端关闭 → 标记 STATE_CLOSE
 *   4. read<0 (EAGAIN) → 没数据了，保持 STATE_READ，等下次 epoll_wait
 */
static void do_read(int epfd, conn_t* c) {
    while (1) {
        if (c->recv_len >= BUF_SIZE - 1) {
            /* 缓冲区满了 */
            printf("[!] fd=%d buffer full, switching to write\n", c->fd);
            c->state = STATE_WRITE;
            return;
        }

        ssize_t n = read(c->fd,
                         c->recv_buf + c->recv_len,
                         BUF_SIZE - 1 - c->recv_len);

        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* 暂时没数据了——保持 STATE_READ */
                return;
            }
            perror("read");
            c->state = STATE_CLOSE;
            return;
        }

        if (n == 0) {
            /* EOF */
            c->state = STATE_CLOSE;
            return;
        }

        c->recv_len += n;
        c->recv_buf[c->recv_len] = '\0';

        /* 检测到 \n → 消息完整 */
        if (memchr(c->recv_buf, '\n', c->recv_len)) {
            c->state = STATE_WRITE;
            return;
        }
    }
}

/*
 * ================================================================
 *  do_write：非阻塞地把 echo 数据写完
 * ================================================================
 */
static void do_write(int epfd, conn_t* c) {
    /* 第一次进入 WRITE 状态时，准备发送缓冲区 */
    if (c->send_len == 0) {
        memcpy(c->send_buf, c->recv_buf, c->recv_len);
        c->send_len  = c->recv_len;
        c->send_done = 0;
    }

    while (c->send_done < c->send_len) {
        ssize_t n = write(c->fd,
                          c->send_buf + c->send_done,
                          c->send_len - c->send_done);

        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* 发送缓冲区满了——等 epoll 通知可写 */
                return;
            }
            perror("write");
            c->state = STATE_CLOSE;
            return;
        }

        c->send_done += n;
    }

    /* 写完了——重置，回到读状态 */
    printf("[>] fd=%d echoed %zu bytes\n", c->fd, c->send_len);
    c->recv_len  = 0;
    c->send_len  = 0;
    c->send_done = 0;
    c->state     = STATE_READ;
}

/*
 * ================================================================
 *  更新 epoll 监听的事件——根据当前状态决定监听 EPOLLIN 还是 EPOLLOUT
 * ================================================================
 */
static void update_epoll_event(int epfd, conn_t* c) {
    struct epoll_event ev;
    ev.data.ptr = c;  /* 这里把 conn 指针绑进去，代替 data.fd */

    switch (c->state) {
    case STATE_READ:
        ev.events = EPOLLIN;  /* 等数据可读 */
        break;
    case STATE_WRITE:
        ev.events = EPOLLIN | EPOLLOUT;  /* 等可读（处理对端关闭） + 等可写 */
        break;
    case STATE_CLOSE:
        ev.events = 0;  /* 不监听 */
        break;
    }

    epoll_ctl(epfd, EPOLL_CTL_MOD, c->fd, &ev);
}

/*
 * ================================================================
 *  main
 * ================================================================
 */
int main(void) {
    int listen_fd, epfd;
    struct sockaddr_in server_addr;

    signal(SIGINT, sigint_handler);

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    { int opt = 1; setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); }
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    listen(listen_fd, 128);
    set_nonblocking(listen_fd);

    epfd = epoll_create1(0);
    {
        struct epoll_event ev;
        ev.events  = EPOLLIN;
        ev.data.fd = listen_fd;
        epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);
    }

    printf("=== Epoll Stateful Echo Server ===\n");
    printf("listening on 0.0.0.0:%d\n", PORT);
    printf("状态机：READ → (读到 \\n) → WRITE → (写完) → READ\n\n");

    struct epoll_event events[MAX_EVENTS];

    while (keep_running) {
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, 1000);
        if (nfds < 0) {
            if (errno == EINTR) break;
            continue;
        }

        for (int i = 0; i < nfds; i++) {
            /* listen_fd 用的是 data.fd，所以区分它和普通 client */
            if (events[i].data.fd == listen_fd) {
                struct sockaddr_in ca;
                socklen_t cl = sizeof(ca);
                int cfd = accept(listen_fd, (struct sockaddr*)&ca, &cl);
                if (cfd < 0) continue;
                set_nonblocking(cfd);

                conn_t* c = calloc(1, sizeof(conn_t));
                c->fd    = cfd;
                c->state = STATE_READ;
                g_conns[cfd % MAX_CLIENTS] = c;

                struct epoll_event ev;
                ev.events  = EPOLLIN;
                ev.data.ptr = c;  /* 用 data.ptr 来存状态指针 */
                epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);

                printf("[+] %s:%d (fd=%d) state=READ\n",
                       inet_ntoa(ca.sin_addr), ntohs(ca.sin_port), cfd);
                continue;
            }

            /* 普通 client：从 data.ptr 拿连接状态 */
            conn_t* c = (conn_t*)events[i].data.ptr;
            if (!c) continue;

            switch (c->state) {
            case STATE_READ:
                do_read(epfd, c);
                if (c->state == STATE_WRITE) {
                    update_epoll_event(epfd, c);
                    do_write(epfd, c);
                    update_epoll_event(epfd, c);
                }
                break;

            case STATE_WRITE:
                do_write(epfd, c);
                update_epoll_event(epfd, c);
                break;

            case STATE_CLOSE:
                break;
            }

            if (c->state == STATE_CLOSE) {
                printf("[-] fd=%d closed\n", c->fd);
                epoll_ctl(epfd, EPOLL_CTL_DEL, c->fd, NULL);
                close(c->fd);
                g_conns[c->fd % MAX_CLIENTS] = NULL;
                free(c);
            }
        }
    }

    close(listen_fd);
    close(epfd);
    printf("\nserver stopped\n");
    return 0;
}
