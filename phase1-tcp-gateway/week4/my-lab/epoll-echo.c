/**
 * epoll-echo.c — 任务一：用 epoll 重写 echo server（多客户端并发）
 *
 * 编译：gcc -Wall -g -O0 -o epoll-echo epoll-echo.c
 * 测试：./epoll-echo
 *       开 3 个终端各运行 nc localhost 8080，三个都能正常 echo
 *
 * strace 观察：
 *   ps aux | grep epoll-echo
 *   strace -e trace=epoll_create,epoll_ctl,epoll_wait,accept,read,write -p <PID>
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
#define BACKLOG     128
#define MAX_EVENTS  64
#define MAX_CLIENTS 1024
#define BUF_SIZE    4096

static volatile int keep_running = 1;

static void sigint_handler(int sig) { (void)sig; keep_running = 0; }

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/*
 * ================================================================
 *  核心：epoll 事件循环
 * ================================================================
 *
 * 逻辑流程：
 *
 *   epoll_wait(epfd, events, MAX_EVENTS, -1)
 *         │
 *         ▼
 *   for each 就绪 fd:
 *         │
 *         ├── fd == listen_fd ──→ accept() ──→ 新 fd 加入 epoll
 *         │
 *         └── fd != listen_fd ──→ read() ──→ 有数据? echo
 *                                        ──→ 返回 0? close + 从 epoll 删
 *         │
 *         ▼
 *   回到 epoll_wait()
 *
 * 和 W3 blocking-server 的区别：
 *   W3: read(client_A) 阻塞 → 回不到 accept → 所有客户端卡住
 *   W4: epoll_wait() 等人 → 就绪的是 client_B → 处理 B，不影响 A
 */

int main(void) {
    int listen_fd, epfd;
    struct sockaddr_in server_addr;

    signal(SIGINT, sigint_handler);

    /* === server 初始化（和 W3 一样） === */
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); exit(1); }

    { int opt = 1; setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    listen(listen_fd, BACKLOG);
    set_nonblocking(listen_fd);

    /* === 创建 epoll === */
    epfd = epoll_create1(0);
    if (epfd < 0) { perror("epoll_create1"); exit(1); }

    /* 把 listen_fd 注册进 epoll */
    struct epoll_event ev;
    ev.events  = EPOLLIN;
    ev.data.fd = listen_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);

    printf("=== Epoll Echo Server ===\n");
    printf("listening on 0.0.0.0:%d\n", PORT);
    printf("测试：开 3 个终端，各运行 nc localhost 8080\n");
    printf("三个应该都能 echo——不会被彼此阻塞\n\n");

    struct epoll_event events[MAX_EVENTS];

    while (keep_running) {
        /* ============================================================
         *  epoll_wait —— 整个系统的核心
         *
         *  参数说明：
         *    epfd     → 要等待的 epoll 实例
         *    events   → 内核把就绪事件写到这里
         *    MAX_EVENTS → 最多返回多少个事件
         *    -1       → 超时时间：-1 = 永远等，0 = 立刻返回，N = 等 N 毫秒
         *
         *  返回值 nfds = 有几个 fd 就绪了
         * ============================================================ */
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, 1000);
        if (nfds < 0) {
            if (errno == EINTR) break;
            perror("epoll_wait");
            continue;
        }

        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;

            if (fd == listen_fd) {
                /* 接线员在敲门——有新连接 */
                struct sockaddr_in client_addr;
                socklen_t clen = sizeof(client_addr);
                int cfd = accept(listen_fd, (struct sockaddr*)&client_addr, &clen);
                if (cfd < 0) continue;

                printf("[+] %s:%d (fd=%d)\n",
                       inet_ntoa(client_addr.sin_addr),
                       ntohs(client_addr.sin_port), cfd);

                set_nonblocking(cfd);
                ev.events  = EPOLLIN;
                ev.data.fd = cfd;
                epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);

            } else {
                /* 某个客户端有数据 */
                char buf[BUF_SIZE];
                ssize_t n = read(fd, buf, sizeof(buf) - 1);

                if (n <= 0) {
                    if (n < 0) perror("read");
                    printf("[-] fd=%d disconnected\n", fd);
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                } else {
                    buf[n] = '\0';
                    if (buf[n-1] == '\n') buf[--n] = '\0';
                    printf("[<] fd=%d: \"%s\" (%zd bytes)\n", fd, buf, n);
                    /* echo */
                    write(fd, buf, n);
                    write(fd, "\n", 1);
                }
            }
        }
    }

    close(listen_fd);
    close(epfd);
    printf("\nserver stopped\n");
    return 0;
}
