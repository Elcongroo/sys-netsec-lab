/**
 * tcp-agent.c — 第一个网络中间件：TCP 代理
 *
 * 架构：
 *   客户端 ←→ 代理(0.0.0.0:9000) ←→ 后端(127.0.0.1:8080)
 *
 * 编译：gcc -Wall -g -O0 -o tcp-agent tcp-agent.c
 * 测试：
 *   终端1: ./epoll-echo                           # 后端，监听 8080
 *   终端2: ./tcp-agent                             # 代理，监听 9000
 *   终端3: nc localhost 9000                       # 客户端，连代理
 *          > hello                                 # 经过代理→后端→代理→返回
 *
 * 新增系统调用：0 个。全部是 W3+W4 已学过的 API 的新组合方式。
 *   新的是"模式"——从 "read(fd) → write(fd)" 变成 "read(A) → write(B)"
 *
 * 和 W4 epoll-echo 的核心差异：
 *   W4: 1 个 fd 自理（读自己的、写回自己）
 *   W5: 2 个 fd 配对（读 A 写到 B，读 B 写到 A）
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

/* ================================================================
 * 配置常量
 * ================================================================ */
#define AGENT_PORT   9000
#define BACKEND_PORT 8080
#define BACKEND_IP   "127.0.0.1"
#define BACKLOG      128
#define MAX_EVENTS   64
#define BUF_SIZE     4096

static volatile int keep_running = 1;

/* ================================================================
 * 工具函数
 * ================================================================ */
static void sigint_handler(int sig) { (void)sig; keep_running = 0; }

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* ================================================================
 * proxy_conn — 连接对结构体
 *
 * 为什么需要这个？
 *   代理管的是"一对"连接，不是一个。当一个 fd 就绪时，你必须
 *   立刻知道它的"配对 fd"是谁——读 client 的数据要写到 backend，
 *   读 backend 的数据要写到 client。
 *
 *   这个结构体就是把两个 fd 背靠背绑在一起。
 *
 * 和 W4 conn_t 的对比：
 *   W4 conn_t:   1 个 fd + 1 个方向的缓冲区（recv + send）
 *   W5 proxy_conn: 2 个 fd + 2 个方向的缓冲区（c→b + b→c）
 *   本质上就是把两个 W4 的 conn_t 拼在一起，做成一个搬运通道。
 * ================================================================ */
struct proxy_conn {
    int    client_fd;     // 客户端这边
    int    backend_fd;    // 后端那边
    int    backend_ready; // 0 = 正在连接, 1 = 已连接好

    /*
     * 两个方向的数据缓冲区
     *
     * 为什么需要缓冲区？
     *   非阻塞 write 可能只写了一半。比如从 client 读了 500 字节，
     *   要写到 backend，但 write(backend, buf, 500) 只写了 300。
     *   剩下 200 字节存在 c2b_buf 里，等 EPOLLOUT 事件再写。
     *
     *   没有缓冲区 → 丢数据。
     */
    char   c2b_buf[BUF_SIZE];   // client → backend 方向
    size_t c2b_len;
    size_t c2b_done;

    char   b2c_buf[BUF_SIZE];   // backend → client 方向
    size_t b2c_len;
    size_t b2c_done;
};

/*
 * 全局连接表——按 fd 索引找到它所属的 proxy_conn
 *
 * 为什么需要？epoll_wait 返回的只是"fd 就绪了"，我们需要
 * 从 fd 立刻找到它属于哪个连接对。data.ptr 可以存指针，
 * 但 accept 时还没创建 proxy_conn，listen_fd 的 data.ptr 没用。
 * 所以用 fd 做索引查表，O(1)。
 */
static struct proxy_conn *g_conns[1024];

/* ================================================================
 * 创建后端连接（非阻塞 connect）——本周最关键的代码段
 *
 * 为什么不能阻塞 connect？
 *   如果后端 3 秒才 accept，这意味着事件循环卡住 3 秒。
 *   其他所有客户端在这 3 秒里全部被晾着——这就是 W3 的死局重演。
 *
 * 非阻塞 connect 的流程：
 *   1. 创建 socket → 设 O_NONBLOCK
 *   2. connect() → 几乎必定返回 -1 + errno=EINPROGRESS
 *      EINPROGRESS = "正在连，别等我"——不是错误
 *   3. 把 fd 注册到 epoll，监听 EPOLLOUT
 *      为什么是 EPOLLOUT？
 *        连接完成 = socket 变成"可写"状态
 *        所以用 EPOLLOUT 来捕捉"连好了"这个事件
 *   4. 等 epoll_wait 返回 EPOLLOUT → 连接好了 →
 *      getsockopt(SO_ERROR) 确认没有错误
 * ================================================================ */
static int backend_connect(const char *ip, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("backend socket"); return -1; }

    set_nonblocking(fd);  // ① 必须先设非阻塞

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    /*
     * ② 发起连接——期望"失败"
     *
     * 非阻塞 connect 在连接建立好之前就返回了。
     * 返回值通常是 -1，errno 是 EINPROGRESS。
     * 如果返回 0 说明连接在本机 localhost 极快完成了（极少见但可能）。
     * 两种情况都不是 bug。
     */
    int ret = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    if (ret < 0 && errno != EINPROGRESS) {
        /*
         * 真正的错误（比如后端没在监听、网络不通）
         * EINPROGRESS 不是错误，是预期行为。其他 errno 才是。
         */
        perror("backend connect");
        close(fd);
        return -1;
    }

    return fd;  // ③ 返回 fd——连接可能还没完成，由 epoll 盯着它
}

/* ================================================================
 * 转发函数：从一个 fd 读到数据，写入另一个 fd
 *
 * 这是代理最核心的操作——把数据从 A 运到 B。
 * 调用者已经通过 epoll_wait 知道 src_fd 可读。
 *
 * 非阻塞模式下 write 可能只写了一部分，
 * 如果你没读完 → 记录到 buffer → 等 EPOLLOUT 再写。
 * 本 demo 简化处理：假设 write 一次写完（localhost 场景足够）。
 * ================================================================ */
static int forward(int src_fd, int dst_fd) {
    char buf[BUF_SIZE];
    ssize_t n = read(src_fd, buf, sizeof(buf));

    if (n <= 0) {
        return -1;  // 对端断开或出错 → 告诉调用者清理
    }

    /*
     * 转发——不修改数据，原封不动搬过去
     * 代理不关心你发的什么，它就是个搬运工。
     */
    ssize_t written = write(dst_fd, buf, n);
    if (written < 0) {
        return -1;
    }

    return n;  // 返回转发了多少字节
}

/* ================================================================
 * 清理：一端断开时，关掉对面，释放资源
 *
 * 为什么不能只 close 断开的 fd？
 *   client 断开 → backend 还开着 → fd 泄漏 + 后端资源浪费
 *   必须"连锁关闭"——一端断，对面也断。
 * ================================================================ */
static void cleanup_conn(int epfd, struct proxy_conn *pc) {
    if (pc->client_fd > 0) {
        epoll_ctl(epfd, EPOLL_CTL_DEL, pc->client_fd, NULL);
        close(pc->client_fd);
        g_conns[pc->client_fd % 1024] = NULL;
    }
    if (pc->backend_fd > 0) {
        epoll_ctl(epfd, EPOLL_CTL_DEL, pc->backend_fd, NULL);
        close(pc->backend_fd);
        g_conns[pc->backend_fd % 1024] = NULL;
    }
    free(pc);
}

/* ================================================================
 * MAIN
 * ================================================================ */
int main(void) {
    int listen_fd, epfd;

    signal(SIGINT, sigint_handler);

    /* ---- 1. 创建监听 socket（W3 已知） ---- */
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); exit(1); }

    { int opt = 1; setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(AGENT_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    listen(listen_fd, BACKLOG);
    set_nonblocking(listen_fd);

    /* ---- 2. 创建 epoll + 注册 listen_fd（W4 已知） ---- */
    epfd = epoll_create1(0);
    if (epfd < 0) { perror("epoll_create1"); exit(1); }

    struct epoll_event ev;
    ev.events  = EPOLLIN;
    ev.data.fd = listen_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);

    printf("=== TCP Agent ===\n");
    printf("listening on 0.0.0.0:%d\n", AGENT_PORT);
    printf("forwarding to %s:%d\n\n", BACKEND_IP, BACKEND_PORT);

    struct epoll_event events[MAX_EVENTS];

    /* ---- 3. 事件循环 ---- */
    while (keep_running) {
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, 1000);
        if (nfds < 0) {
            if (errno == EINTR) break;
            continue;
        }

        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;

            /* ====================================================
             * 分支 A：新客户端连上来
             * ==================================================== */
            if (fd == listen_fd) {
                struct sockaddr_in client_addr;
                socklen_t clen = sizeof(client_addr);
                int cfd = accept(listen_fd, (struct sockaddr*)&client_addr, &clen);
                if (cfd < 0) continue;

                printf("[+] client %s:%d (fd=%d)\n",
                       inet_ntoa(client_addr.sin_addr),
                       ntohs(client_addr.sin_port), cfd);

                /*
                 * 客户端来了 → 不再 echo，而是去连后端
                 *
                 * accept 之后的三步：
                 * ① client 设非阻塞
                 * ② 连后端（非阻塞 connect）
                 * ③ 创建连接对 proxy_conn，把两个 fd 绑在一起
                 */
                set_nonblocking(cfd);

                int bfd = backend_connect(BACKEND_IP, BACKEND_PORT);
                if (bfd < 0) {
                    /*
                     * 后端连不上 → 拒绝这个客户端
                     * 不能让客户端干等着。
                     */
                    printf("[!] backend connect failed, closing client fd=%d\n", cfd);
                    close(cfd);
                    continue;
                }

                /*
                 * 创建连接对——这是代理和 echo server 的本质区别
                 */
                struct proxy_conn *pc = calloc(1, sizeof(*pc));
                pc->client_fd  = cfd;
                pc->backend_fd = bfd;
                pc->backend_ready = (bfd > 0) ? 1 : 0;

                /*
                 * 把两个 fd 都注册到 epoll
                 *
                 * client_fd  → 等 EPOLLIN（客户端发数据）
                 * backend_fd → 等 EPOLLIN（后端回数据）
                 *
                 * 重点：两个 fd 绑同一个 proxy_conn 指针！
                 * 这样无论哪个 fd 就绪，都能立刻找到它的配对。
                 */
                g_conns[cfd % 1024] = pc;
                g_conns[bfd % 1024] = pc;

                ev.events  = EPOLLIN;
                ev.data.ptr = pc;
                epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);
                epoll_ctl(epfd, EPOLL_CTL_ADD, bfd, &ev);

                printf("  -> backend fd=%d, pair created\n", bfd);

            } else {
                /* ====================================================
                 * 分支 B / C：客户端或后端有数据
                 *
                 * 通过 data.ptr 拿到连接对 → 判断是哪一端 → 转发到另一端
                 * ==================================================== */
                struct proxy_conn *pc = events[i].data.ptr;
                if (!pc) continue;

                if (fd == pc->client_fd) {
                    /*
                     * 分支 B：客户端发了数据 → 转发给后端
                     */
                    int n = forward(pc->client_fd, pc->backend_fd);
                    if (n < 0) {
                        printf("[-] client fd=%d disconnected, cleaning pair\n", pc->client_fd);
                        cleanup_conn(epfd, pc);
                    } else {
                        printf("[→] client→backend: %d bytes\n", n);
                    }
                } else if (fd == pc->backend_fd) {
                    /*
                     * 分支 C：后端回了数据 → 转发给客户端
                     */
                    int n = forward(pc->backend_fd, pc->client_fd);
                    if (n < 0) {
                        printf("[-] backend fd=%d disconnected, cleaning pair\n", pc->backend_fd);
                        cleanup_conn(epfd, pc);
                    } else {
                        printf("[←] backend→client: %d bytes\n", n);
                    }
                }
            }
        }
    }

    close(listen_fd);
    close(epfd);
    printf("\nagent stopped\n");
    return 0;
}
