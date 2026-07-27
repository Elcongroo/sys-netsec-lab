/**
 * echod-server.c — TCP Echo Server（带部分读处理）
 *
 * 功能：监听 0.0.0.0:8080，循环 accept 客户端，收到什么 echo 回去什么。
 *       **第一次引入 "robust read" 模式**——用循环确保读完。
 *
 * 编译：gcc -Wall -g -O0 -o echod-server echod-server.c
 * 测试：./echod-server
 *       nc localhost 8080        ← 另一个终端
 * 退出：Ctrl+C
 *
 * 关键设计决策：
 *   1. read() 不保证一次读完所有数据 → 用循环 read_wrapper
 *   2. write() 同样可能部分写 → 用循环 write_wrapper
 *   3. 这两个 wrapper 是生产级代码的基础模式
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
#define BUF_SIZE 4096

/* 全局变量：用于信号处理 */
static volatile int keep_running = 1;

void sigint_handler(int sig) {
    (void)sig;
    keep_running = 0;
}

/*
 * ================================================================
 *  核心工具函数：鲁棒的 read / write
 * ================================================================
 *
 * 为什么需要这两个 wrapper？
 *
 * 普通的 read(fd, buf, size) 可能返回 3 而不是你期望的 1024。
 * 原因：TCP 是字节流，数据可能分多个 TCP 段到达，
 *       每次 read 只能拿"当前已到达"的数据。
 *
 * 普通的 write(fd, buf, size) 也可能只写一部分（socket buffer 满了）。
 *
 * 解决方案：循环读/写，直到读完/写完。
 */

/* 循环读：确保读够 n 字节，除非 EOF */
ssize_t read_full(int fd, void* buf, size_t n) {
    size_t total = 0;
    while (total < n) {
        ssize_t nr = read(fd, (char*)buf + total, n - total);
        if (nr < 0) {
            if (errno == EINTR) continue;  /* 被信号打断，重试 */
            return -1;                      /* 真正的错误 */
        }
        if (nr == 0) {
            break;  /* EOF：对端关闭了连接 */
        }
        total += nr;
    }
    return (ssize_t)total;
}

/* 循环写：确保写完 n 字节 */
ssize_t write_full(int fd, const void* buf, size_t n) {
    size_t total = 0;
    while (total < n) {
        ssize_t nw = write(fd, (const char*)buf + total, n - total);
        if (nw < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (nw == 0) {
            break;  /* socket 写端关闭 */
        }
        total += nw;
    }
    return (ssize_t)total;
}

/*
 * ================================================================
 *  处理一个客户端连接的完整生命周期
 * ================================================================
 */
void handle_client(int client_fd, struct sockaddr_in* client_addr) {
    char buf[BUF_SIZE];
    ssize_t n;

    printf("  [client %s:%d] connected\n",
           inet_ntoa(client_addr->sin_addr),
           ntohs(client_addr->sin_port));

    while (1) {
        n = read(client_fd, buf, sizeof(buf) - 1);  /* 留 1 字节给 \0 */

        if (n < 0) {
            perror("  read error");
            break;
        }

        if (n == 0) {
            /* read 返回 0 = 对端关闭了连接（FIN 或 close） */
            printf("  [client %s:%d] disconnected (EOF)\n",
                   inet_ntoa(client_addr->sin_addr),
                   ntohs(client_addr->sin_port));
            break;
        }

        buf[n] = '\0';
        /* 去掉末尾换行，方便打印 */
        if (n > 0 && buf[n - 1] == '\n') {
            buf[n - 1] = '\0';
            n--;
        }

        printf("  [client %s:%d] received %zd bytes: \"%s\"\n",
               inet_ntoa(client_addr->sin_addr),
               ntohs(client_addr->sin_port), n + 1, buf);  /* +1 因为我们去掉了 \n */

        /* echo 回去：用 write_full 确保写完 */
        buf[n] = '\n';  /* 把换行加回去 */
        n++;
        if (write_full(client_fd, buf, (size_t)n) < 0) {
            perror("  write error");
            break;
        }
    }

    close(client_fd);
    printf("  [client %s:%d] closed\n",
           inet_ntoa(client_addr->sin_addr),
           ntohs(client_addr->sin_port));
}

/*
 * ================================================================
 *  main：创建 server，循环 accept
 * ================================================================
 */
int main(void) {
    int listen_fd;
    struct sockaddr_in server_addr;

    /* Ctrl+C 优雅退出 */
    signal(SIGINT, sigint_handler);

    /* Step 1: socket */
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); exit(1); }
    printf("[1] socket created, fd=%d\n", listen_fd);

    /* 允许端口重用（server 重启后不用等 TIME_WAIT） */
    {
        int opt = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    }

    /* Step 2: bind */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind"); exit(1);
    }
    printf("[2] bound to 0.0.0.0:%d\n", PORT);

    /* Step 3: listen */
    if (listen(listen_fd, BACKLOG) < 0) {
        perror("listen"); exit(1);
    }
    printf("[3] listening (backlog=%d), press Ctrl+C to stop\n\n", BACKLOG);

    /* Step 4: accept 循环 */
    while (keep_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd;

        printf("[4] waiting for client...\n");
        client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);

        if (client_fd < 0) {
            if (errno == EINTR) break;  /* Ctrl+C 打断 */
            perror("accept");
            continue;
        }

        /* Step 5: 处理这个客户端 */
        handle_client(client_fd, &client_addr);
    }

    /* 清理 */
    close(listen_fd);
    printf("\nserver stopped\n");
    return 0;
}
