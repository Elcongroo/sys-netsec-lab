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

/*
 * set_nonblocking —— 把 fd 设为非阻塞模式
 *
 * 为什么要设非阻塞？
 *   反面教材：阻塞 fd 在 epoll 下的隐藏陷阱
 *     epoll_wait 说"fd 可读了" → 你去 read(fd)
 *     但在你调用 read 之前的那几微秒，如果客户端发了 RST（取消连接），
 *     read 可能返回 0（EOF）甚至 -1。
 *     阻塞模式下，read 会一直等到有数据——但连接已经被取消了，
 *     永远不会再有数据，于是死等。
 *
 *   所以：epoll 给的是"通知"，不是"保证"。
 *   非阻塞就是给这个不确定性兜底的——read 没数据立刻返回 EAGAIN，不卡。
 */
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
 *   epoll_wait(epfd, events, MAX_EVENTS, timeout)
 *         │
 *         │  "内核，谁有动静？"
 *         │  返回就绪 fd 的列表（只返回有动静的，不遍历全部）
 *         ▼
 *   for each 就绪 fd:
 *         │
 *         ├── fd == listen_fd
 *         │     → 接线员在敲门 → accept() → 新 cfd 挂进 epoll → 回去等
 *         │      （注意：这里不读数据！数据由 else 分支处理）
 *         │
 *         └── fd != listen_fd
 *               → 某个客户端有数据 → read() → echo → 回去等
 *                                  → read() 返回 0/错误 → close + 从 epoll 移除
 *         │
 *         ▼
 *   回到 epoll_wait()  ← 等待下一批事件
 *
 * 和 W3 blocking-server 的本质区别：
 *   W3: read(client_A) 阻塞 → 线程卡住 → 回不到 accept → 所有客户端死等
 *   W4: epoll_wait() 等"任何人" → 谁就绪处理谁 → 处理完立刻回去等
 *       不会因为 client_A 没动静就忽略 client_B
 */

int main(void) {
    int listen_fd, epfd;
    struct sockaddr_in server_addr;

    signal(SIGINT, sigint_handler);

    /* ================================================================
     * server 初始化（socket → bind → listen → 非阻塞）
     * 和 W3 完全一样，不赘述
     * ================================================================ */
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

    /* ================================================================
     * 创建 epoll 实例 + 注册 listen_fd
     *
     * epoll_create1(0) → 返回一个"epoll fd"
     *   这个 fd 不指向磁盘文件，指向内核里的一个 epoll 监控表。
     *   你可以像普通 fd 一样 close 它。Linux "一切皆文件"的体现。
     *
     * epoll_ctl(ADD) → 把 listen_fd 挂进监控表
     *   EPOLLIN = 关注"可读"事件
     *   对 listen_fd 来说，"可读" = 有新连接在 backlog 队列里等着
     * ================================================================ */
    epfd = epoll_create1(0);
    if (epfd < 0) { perror("epoll_create1"); exit(1); }

    struct epoll_event ev;
    ev.events  = EPOLLIN;          // 关注"可读"
    ev.data.fd = listen_fd;        // 把 listen_fd 绑进事件（epoll_wait 返回时用）
    epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);

    printf("=== Epoll Echo Server ===\n");
    printf("listening on 0.0.0.0:%d\n", PORT);
    printf("测试：开 3 个终端，各运行 nc localhost 8080\n");
    printf("三个应该都能 echo——不会被彼此阻塞\n\n");

    struct epoll_event events[MAX_EVENTS];

    while (keep_running) {
        /*
         * epoll_wait —— 整个系统的核心
         *
         *   参数：
         *     epfd      → 要等待的 epoll 实例
         *     events    → 内核把就绪事件写进这个数组
         *     MAX_EVENTS → 最多返回多少个事件（数组容量）
         *     1000      → 超时毫秒数
         *                  -1 = 永远等（纯服务器用）
         *                   0 = 立刻返回，不等待（有定时任务时用）
         *                1000 = 等 1 秒后返回 0（让 Ctrl+C 能在 1 秒内生效）
         *
         *   返回值 nfds：
         *     > 0 → 有 nfds 个 fd 就绪了，遍历 events[0..nfds-1]
         *       0 → 超时，这段时间内没有任何 fd 有动静
         *      -1 → 出错，（常见：EINTR = 被 Ctrl+C 打断）
         */
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, 1000);
        if (nfds < 0) {
            if (errno == EINTR) break;    // Ctrl+C → 退出
            perror("epoll_wait");
            continue;
        }

        /*
         * 遍历所有就绪的 fd
         *
         * epoll_wait 只返回有动静的 fd，不返回所有注册的 fd。
         * 这就是 epoll 比 select 快的原因：O(就绪数) 而不是 O(总数)。
         * 10000 个连接中只有 3 个有数据 → 只遍历 3 个，不是 10000 个。
         */
        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;

            if (fd == listen_fd) {
                /*
                 * ================================================
                 * fd == listen_fd → "接线员在敲门"
                 * ================================================
                 *
                 * 面试常考题：epoll 模式下 accept 应该做什么？
                 * 答案：只做两件事 —— accept 接进来 + 注册到 epoll。
                 * 不读数据！不 echo！数据由 else 分支在下一次 epoll_wait 时处理。
                 *
                 * ================================================
                 * 反面教材（q4 的 bug 代码）：
                 * ================================================
                 *   int cfd = accept(listen_fd, NULL, NULL);
                 *   char buf[1024];
                 *   int n = read(cfd, buf, sizeof(buf));  // ← 阻塞！
                 *   write(cfd, buf, n);
                 *   close(cfd);
                 *
                 *   三个致命错误：
                 *   ① cfd 没设非阻塞 → read 卡住 → 整个事件循环死掉
                 *   ② cfd 没注册到 epoll → 客户端一辈子只服务一次
                 *      （如果数据分两次到达，第二次永远没人读）
                 *   ③ accept 里包揽了 read + write + close → 回到 W3 阻塞模型
                 * ================================================
                 */
                struct sockaddr_in client_addr;
                socklen_t clen = sizeof(client_addr);
                int cfd = accept(listen_fd, (struct sockaddr*)&client_addr, &clen);
                if (cfd < 0) continue;

                printf("[+] %s:%d (fd=%d)\n",
                       inet_ntoa(client_addr.sin_addr),
                       ntohs(client_addr.sin_port), cfd);

                /*
                 * accept 后的三步固定流程，一步都不能少：
                 *
                 * ① set_nonblocking(cfd)
                 *    把新客户端也设成非阻塞。
                 *    原因和 listen_fd 一样：epoll 通知 ≠ 保证数据一定在。
                 *
                 * ② ev.events = EPOLLIN; ev.data.fd = cfd;
                 *    配置：关注这个 fd 的"可读"事件。
                 *    对 client_fd 来说，"可读" = 客户端发了数据过来。
                 *
                 * ③ epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev)
                 *    把 cfd 注册到 epoll 监控表。
                 *    这是最容易被漏掉的一步——漏了就回到 W3 阻塞模型。
                 */
                set_nonblocking(cfd);   // ① 非阻塞
                ev.events  = EPOLLIN;   // ② 关注可读
                ev.data.fd = cfd;       //    绑定 fd
                epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);  // ③ 注册
                // 做完这三步立刻回到 while 循环顶部的 epoll_wait，
                // 不在这里读数据！

            } else {
                /*
                 * ================================================
                 * fd != listen_fd → "某个客户端有数据到达"
                 * ================================================
                 *
                 * 这是 epoll 服务器运行时最常走到的分支。
                 * 每一个已连接的客户端，每次发数据，都会触发这里。
                 *
                 * 注意：buf 是栈上的局部变量，每次循环都是全新的。
                 * 这意味着我们假设"一次 read 就能拿到完整消息"。
                 * 对 echo server 够了，但对 HTTP 请求等场景，
                 * 需要 conn_t 结构体跨事件保存读取进度 —— 见 epoll-stateful.c
                 */
                char buf[BUF_SIZE];
                ssize_t n = read(fd, buf, sizeof(buf) - 1);

                if (n <= 0) {
                    /*
                     * read 返回 <= 0 → 客户端断开
                     *
                     * n == 0：客户端正常关闭（发送了 FIN）
                     * n < 0：read 错误
                     *   非阻塞模式下一般不会到这里
                     *   （因为 epoll_wait 已经保证可读才通知你），
                     *   但保留判断以防万一。
                     *
                     * 断开后的清理步骤（顺序重要）：
                     *   ① epoll_ctl(DEL) — 从 epoll 监控表移除
                     *      必须先删再关！如果先 close，fd 可能被复用，
                     *      epoll 还在监听同一个 fd 号但指向了别的资源
                     *   ② close(fd) — 释放内核资源
                     */
                    if (n < 0) perror("read");
                    printf("[-] fd=%d disconnected\n", fd);
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);  // ① 先删
                    close(fd);                                  // ② 再关
                } else {
                    /*
                     * 读到数据 → echo 回去
                     *
                     * 简化处理：去掉客户端发的 \n，再补一个 \n 回去。
                     * 真正的 echo 应该原样返回原始数据，这里是演示用的简化版。
                     *
                     * 注意：这里直接 write，假设一次写完。
                     * 在 LT（水平触发）模式下通常没问题。
                     * 但在 ET（边缘触发）或高速场景下，
                     * write 可能只写了一部分——需要循环写直到全部写完，
                     * 见 epoll-stateful.c 的 do_write()。
                     */
                    buf[n] = '\0';
                    if (buf[n-1] == '\n') buf[--n] = '\0';
                    printf("[<] fd=%d: \"%s\" (%zd bytes)\n", fd, buf, n);
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
