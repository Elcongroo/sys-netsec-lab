/**
 * blocking-server.c — 任务三：演示单线程阻塞模型的致命缺陷
 *
 * 这个 server 故意写成"一次只能服务一个客户端"的模式。
 * 用它来观察：第二个客户端连不上、连上了也收不到 echo。
 *
 * 编译：gcc -Wall -g -O0 -o blocking-server blocking-server.c
 *
 * 实验步骤：
 *   终端1：./blocking-server
 *   终端2：nc localhost 8080
 *   终端3：nc localhost 8080        ← 观察：连得上吗？
 *   终端4：nc localhost 8080        ← 观察：连得上吗？
 *
 * 然后在终端2 输入一些文字，再按 Ctrl+C 断开——
 * 观察终端2 断开后，终端3 或终端4 是否立刻被 accept？
 *
 * 关键观察：
 *   - 终端2 不输入 → server 在 read() 阻塞 → 终端3/4 永远进不来
 *   - 终端2 断开 → server 回到 accept() → 终端3/4 中的一个被 accept
 *   - 内核里 backlog 队列在排队，但 server 根本没在看
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT    8080
#define BACKLOG 128

static volatile int keep_running = 1;

void sigint_handler(int sig) {
    (void)sig;
    keep_running = 0;
}

int main(void) {
    int listen_fd;
    struct sockaddr_in server_addr;

    signal(SIGINT, sigint_handler);

    /* socket + setsockopt + bind + listen */
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
    printf("Blocking Server listening on 0.0.0.0:%d\n", PORT);
    printf("(单线程阻塞模式 —— 一个客户端卡住，全部卡住)\n\n");

    while (keep_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        char buf[1024];
        char client_name[64];
        int client_fd;
        ssize_t n;

        /* accept：阻塞，等第一个客户端 */
        printf("[accept] 等待新客户端...\n");
        client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) break;
            perror("accept");
            continue;
        }

        snprintf(client_name, sizeof(client_name), "%s:%d",
                 inet_ntoa(client_addr.sin_addr),
                 ntohs(client_addr.sin_port));
        printf("[accept] %s 已连接, fd=%d\n", client_name, client_fd);

        /*
         * read：阻塞，等这个客户端发数据
         * ⚠️ 问题就在这里！
         * 在 read() 返回之前，无法回到上面的 accept()——
         * 所以其他客户端只能在内核 backlog 队列里排队。
         */
        printf("[read]   等待 %s 发送数据 (阻塞中，其他客户端无法被 accept)...\n", client_name);
        printf("          (在另一个终端运行 nc localhost 8080 试试)\n");

        n = read(client_fd, buf, sizeof(buf) - 1);
        if (n < 0) {
            perror("read");
            close(client_fd);
            continue;
        }
        if (n == 0) {
            printf("[read]   %s 关闭了连接\n", client_name);
            close(client_fd);
            continue;
        }

        buf[n] = '\0';
        if (n > 0 && buf[n - 1] == '\n') buf[n - 1] = '\0';
        printf("[read]   收到来自 %s: \"%s\" (%zd bytes)\n", client_name, buf, n);

        /* echo 回去 */
        buf[n] = '\0';
        size_t wlen = strlen(buf);
        buf[wlen] = '\n';
        write(client_fd, buf, wlen + 1);

        printf("[write]  已 echo 回 %s\n", client_name);

        /*
         * 关闭这个客户端，循环回到 accept —— 下一个排队的客户才能进来
         */
        close(client_fd);
        printf("[close]  %s 已关闭\n\n", client_name);
    }

    close(listen_fd);
    printf("\nserver stopped\n");
    return 0;
}
