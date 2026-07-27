/**
 * echod-client.c — TCP Echo Client（带部分读处理）
 *
 * 功能：连接 127.0.0.1:8080，发送用户输入，接收 echo 响应。
 *       使用 read_full 确保读完 server 的响应。
 *
 * 编译：gcc -Wall -g -O0 -o echod-client echod-client.c
 * 使用：./echod-client
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT      8080
#define BUF_SIZE  4096

/* 同 server 的 read_full：循环读到 n 字节或 EOF */
ssize_t read_full(int fd, void* buf, size_t n) {
    size_t total = 0;
    while (total < n) {
        ssize_t nr = read(fd, (char*)buf + total, n - total);
        if (nr < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (nr == 0) break;
        total += nr;
    }
    return (ssize_t)total;
}

int main(void) {
    int sock_fd;
    struct sockaddr_in server_addr;
    char send_buf[BUF_SIZE];
    char recv_buf[BUF_SIZE];

    /* socket */
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) { perror("socket"); exit(1); }

    /* connect */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(PORT);
    if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) <= 0) {
        perror("inet_pton"); exit(1);
    }

    printf("connecting to 127.0.0.1:%d...\n", PORT);
    if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect"); exit(1);
    }
    printf("connected!\n\n");

    /* 交互循环 */
    printf("Type message + Enter ('quit' to exit):\n");

    while (1) {
        printf("> ");
        fflush(stdout);

        /* 读用户输入 */
        if (fgets(send_buf, sizeof(send_buf), stdin) == NULL) {
            printf("\n");
            break;
        }

        size_t len = strlen(send_buf);
        if (len > 0 && send_buf[len - 1] == '\n') send_buf[len - 1] = '\0';
        if (strcmp(send_buf, "quit") == 0) break;

        /* 发送 */
        size_t send_len = strlen(send_buf);
        send_buf[send_len] = '\n';  /* 加上换行，server 需要 */
        if (write(sock_fd, send_buf, send_len + 1) < 0) {
            perror("write");
            break;
        }

        /*
         * 接收 echo 响应。
         * 这里用 read_full 确保读完一行（以 \n 结尾），
         * 但实际上 echo 协议是"发什么回什么"——为了安全，用循环读。
         *
         * 简化处理：server 是 echo 一行回一行，所以读 1024 字节够了。
         * 真正复杂协议需要用定长头 + 变长体的方式。
         */
        ssize_t n = read_full(sock_fd, recv_buf, BUF_SIZE - 1);
        if (n < 0) {
            perror("read");
            break;
        }
        if (n == 0) {
            printf("server closed connection\n");
            break;
        }

        recv_buf[n] = '\0';
        if (n > 0 && recv_buf[n - 1] == '\n') recv_buf[n - 1] = '\0';
        printf("echo: %s\n", recv_buf);
    }

    close(sock_fd);
    printf("bye\n");
    return 0;
}
