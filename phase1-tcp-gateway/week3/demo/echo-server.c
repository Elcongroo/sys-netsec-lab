/**
 * echo-server.c — 最简单的 TCP Echo Server
 *
 * 功能：监听 0.0.0.0:8080，接受一个客户端连接，
 *       收到什么就 echo 回去什么。
 *
 * 编译：gcc -Wall -g -O0 -o echo-server echo-server.c
 *
 * 测试：在一个终端运行 ./echo-server
 *       在另一个终端运行 nc localhost 8080
 *       输入任意文本，按回车——server 会 echo 回来
 *
 * strace：ps aux | grep echo-server 找到 PID
 *        strace -p <PID> -e trace=network
 *
 * 注意：这个版本一次只处理一个客户端，处理完就退出。
 *       这是为了让你看清楚每一步——后续再改成循环 accept。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT    8080
#define BACKLOG 128     /* 内核等待队列最大长度 */

int main(void) {
    int listen_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len;
    char buf[1024];
    ssize_t n;

    /* Step 1: 创建 socket */
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        exit(1);
    }
    printf("[1/5] socket created, fd=%d\n", listen_fd);

    /* Step 2: 绑定地址 */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(PORT);       /* htons: 转为网络字节序 */
    server_addr.sin_addr.s_addr = INADDR_ANY;         /* 监听所有网卡 */

    if (bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        exit(1);
    }
    printf("[2/5] bind to 0.0.0.0:%d\n", PORT);

    /* Step 3: 开始监听 */
    if (listen(listen_fd, BACKLOG) < 0) {
        perror("listen");
        exit(1);
    }
    printf("[3/5] listening (backlog=%d)...\n", BACKLOG);

    /* Step 4: 接受连接（阻塞！） */
    printf("[4/5] waiting for client (blocking)...\n");
    client_len = sizeof(client_addr);
    client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
    if (client_fd < 0) {
        perror("accept");
        exit(1);
    }
    printf("[4/5] accepted connection from %s:%d, client_fd=%d\n",
           inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), client_fd);

    /* Step 5: 通信 —— echo 循环 */
    printf("[5/5] echo loop (type 'quit' to disconnect)\n");
    while (1) {
        n = read(client_fd, buf, sizeof(buf) - 1);
        if (n < 0) {
            perror("read");
            break;
        }
        if (n == 0) {
            printf("  client closed connection\n");
            break;
        }

        buf[n] = '\0';                               /* 安全：read 不保证 \0 */
        printf("  received %zd bytes: %s", n, buf);

        /* echo 回去 */
        if (write(client_fd, buf, n) != n) {
            perror("write");
            break;
        }
    }

    /* 清理 */
    close(client_fd);
    close(listen_fd);
    printf("server done\n");
    return 0;
}
