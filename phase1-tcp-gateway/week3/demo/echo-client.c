/**
 * echo-client.c — TCP Echo Client
 *
 * 功能：连接 127.0.0.1:8080，发送用户输入，打印 server 的 echo 响应。
 *
 * 编译：gcc -Wall -g -O0 -o echo-client echo-client.c
 * 使用：./echo-client
 *       输入文本，按回车发送，server 会 echo 回来
 *       按 Ctrl+D 或输入 quit 退出
 *
 * 注意：这里故意用简单的 write/read，没有处理"部分读"。
 *       任务一中你需要改进它——用循环 read 确保读完所有数据。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 8080

int main(void) {
    int sock_fd;
    struct sockaddr_in server_addr;
    char buf[1024];

    /* Step 1: 创建 socket */
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("socket");
        exit(1);
    }
    printf("socket created, fd=%d\n", sock_fd);

    /* Step 2: 连接 server */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(PORT);
    if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        exit(1);
    }

    printf("connecting to 127.0.0.1:%d...\n", PORT);
    if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        exit(1);
    }
    printf("connected!\n\n");

    /* Step 3: 交互循环 */
    printf("Type a message and press Enter (Ctrl+D or 'quit' to exit):\n");
    while (1) {
        printf("> ");
        fflush(stdout);

        if (fgets(buf, sizeof(buf), stdin) == NULL) {
            printf("\nbye\n");
            break;
        }

        /* 去掉末尾换行 */
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
        if (strcmp(buf, "quit") == 0) break;

        /* 发送到 server */
        if (write(sock_fd, buf, strlen(buf)) < 0) {
            perror("write");
            break;
        }

        /* 接收 echo 响应 */
        ssize_t n = read(sock_fd, buf, sizeof(buf) - 1);
        if (n < 0) {
            perror("read");
            break;
        }
        if (n == 0) {
            printf("server closed connection\n");
            break;
        }

        buf[n] = '\0';
        printf("echo: %s\n", buf);
    }

    close(sock_fd);
    return 0;
}
