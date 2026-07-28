/**
 * epoll-server.c — epoll 并发 Echo Server
 *
 * 一个线程，同时处理多个客户端——谁有数据就服务谁。
 *
 * 编译：gcc -Wall -g -O0 -o epoll-server epoll-server.c
 * 测试：./epoll-server
 *       开 3 个终端各运行 nc localhost 8080，三个都能正常 echo
 *
 * 与 W3 blocking-server 的核心差异：
 *   W3:  accept → read(阻塞) → write → close → 回到 accept
 *   W4:  epoll_wait(阻塞) → 遍历就绪 fd → accept 或 read → 回到 epoll_wait
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT        8080
#define BACKLOG     128
#define MAX_EVENTS  64
#define BUF_SIZE    4096

static volatile int keep_running = 1;

void sigint_handler(int sig) {
    (void)sig;
    keep_running = 0;
}

/* ================================================================
 * 把一个 fd 设为非阻塞模式
 *
 * 为什么需要非阻塞？
 *   默认 socket 是阻塞的——read() 没数据就卡住。
 *   在 epoll 模式下，epoll_wait 说"有数据"了我们才去 read，
 *   所以理论上不会卡。但为了安全（防止边缘情况），设成非阻塞。
 * ================================================================ */
static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* ================================================================
 * 在 epoll 中注册一个 fd
 * ================================================================ */
static void epoll_add_fd(int epfd, int fd) {
    struct epoll_event ev;
    ev.events  = EPOLLIN;        /* 关注"可读"事件 */
    ev.data.fd = fd;             /* 把 fd 绑定进去 */
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        perror("epoll_ctl: add");
    }
}

/* ================================================================
 * 处理新连接：accept → 设非阻塞 → 注册到 epoll
 * ================================================================ */
static void handle_accept(int epfd, int listen_fd) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
    if (client_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            perror("accept");
        return;
    }

    printf("[+] client connected: %s:%d (fd=%d)\n",
           inet_ntoa(client_addr.sin_addr),
           ntohs(client_addr.sin_port),
           client_fd);

    set_nonblocking(client_fd);
    epoll_add_fd(epfd, client_fd);
}

/* ================================================================
 * 处理已有客户端的数据
 * ================================================================ */
static void handle_client(int epfd, int fd) {
    char buf[BUF_SIZE];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);

    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            /* 非阻塞模式下暂时没数据——不是错误 */
            return;
        }
        perror("read");
        goto disconnect;
    }

    if (n == 0) {
        /* EOF：客户端关闭了连接 */
        goto disconnect;
    }

    buf[n] = '\0';
    if (n > 0 && buf[n - 1] == '\n') buf[n - 1] = '\0';
    printf("[<] fd=%d: \"%s\" (%zd bytes)\n", fd, buf, n);

    /* echo 回去 */
    buf[strlen(buf)] = '\n';  /* 补回换行 */
    n = strlen(buf);
    if (write(fd, buf, n) < 0) {
        perror("write");
        goto disconnect;
    }

    printf("[>] fd=%d: echoed %zd bytes\n", fd, n);
    return;

disconnect:
    printf("[-] fd=%d disconnected\n", fd);
    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);  /* 从 epoll 移除 */
    close(fd);
}

/* ================================================================
 * main：创建 epoll → 注册 listen_fd → 事件循环
 * ================================================================ */
int main(void) {
    int listen_fd, epfd;
    struct sockaddr_in server_addr;
    struct epoll_event events[MAX_EVENTS];

    signal(SIGINT, sigint_handler);

    /* ---- 和 W3 完全一样的 server 初始化 ---- */
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); exit(1); }

    {
        int opt = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind"); exit(1);
    }
    if (listen(listen_fd, BACKLOG) < 0) {
        perror("listen"); exit(1);
    }

    set_nonblocking(listen_fd);

    /* ---- 新的部分：创建 epoll ---- */
    epfd = epoll_create1(0);
    if (epfd < 0) { perror("epoll_create1"); exit(1); }

    epoll_add_fd(epfd, listen_fd);  /* 先把 listen_fd 加入监控 */

    printf("Epoll Echo Server listening on 0.0.0.0:%d\n", PORT);
    printf("(单线程 + epoll — 同时处理多个客户端)\n\n");

    /* ---- 事件循环（和 W3 的关键区别） ---- */
    while (keep_running) {
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, 1000);
        //                                          │
        //                               timeout=1000ms(每秒检查一次 keep_running)

        if (nfds < 0) {
            if (errno == EINTR) break;  /* Ctrl+C */
            perror("epoll_wait");
            continue;
        }

        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;

            if (fd == listen_fd) {
                handle_accept(epfd, listen_fd);
            } else {
                handle_client(epfd, fd);
            }
        }
    }

    close(listen_fd);
    close(epfd);
    printf("\nserver stopped\n");
    return 0;
}
