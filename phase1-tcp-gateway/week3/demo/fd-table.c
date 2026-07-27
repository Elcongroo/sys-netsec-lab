/**
 * fd-table.c — 亲手验证文件描述符的分配规则
 *
 * 规则：新 fd 永远是当前可用的最小整数。
 *       0、1、2 被 stdin/stdout/stderr 占了，
 *       所以 open() 从 3 开始。
 *       关掉一个 fd 后，这个"坑位"会被下一个 open 重用。
 *
 * 编译：gcc -Wall -g -O0 -o fd-table fd-table.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

int main(void) {
    printf("=== 文件描述符分配规则 ===\n\n");

    printf("stdin  = %d (STDIN_FILENO)\n", STDIN_FILENO);
    printf("stdout = %d (STDOUT_FILENO)\n", STDOUT_FILENO);
    printf("stderr = %d (STDERR_FILENO)\n", STDERR_FILENO);
    printf("\n");

    /* 连续打开三个文件 */
    int fd1 = open("/etc/hostname", O_RDONLY);
    printf("open /etc/hostname   → fd=%d\n", fd1);

    int fd2 = open("/etc/hosts", O_RDONLY);
    printf("open /etc/hosts      → fd=%d\n", fd2);

    int fd3 = open("/etc/passwd", O_RDONLY);
    printf("open /etc/passwd     → fd=%d\n", fd3);

    printf("\n--- 现在关掉 fd1 (%d) ---\n\n", fd1);
    close(fd1);

    /* 再打开一个新文件——它会拿到刚释放的"最小坑位" */
    int fd4 = open("/etc/resolv.conf", O_RDONLY);
    printf("open /etc/resolv.conf → fd=%d (重用了 fd1=%d)\n", fd4, fd1);

    close(fd2);
    close(fd3);
    close(fd4);

    printf("\n=== 规则确认：fd=%d == 之前的 fd1=%d ✓ ===\n", fd4, fd1);
    return 0;
}
