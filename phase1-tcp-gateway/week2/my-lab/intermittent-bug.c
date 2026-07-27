/**
 * intermittent-bug.c — 一字节堆缓冲区溢出（故意制造）
 *
 * 场景：从"网络"接收数据，向 malloc 的 8 字节缓冲区写入了 9 个字节。
 *       多出的那一个字节写入了堆上相邻内存区域。
 *
 * 为什么有时崩、有时不崩？（无 ASan 时）
 *   - 溢出的一个字节写入相邻 chunk 的元数据或数据区
 *   - 在 glibc tcache 中，小块释放不检查 chunk 一致性
 *   - 溢出的字节是否触发崩溃取决于：堆布局、tcache 状态、写入的值
 *   - 大部分时候程序"正常"运行，但堆已经悄悄被破坏了
 *   - 当 tcache bin 满了、chunk 被整合时 → 潜在的 segfault
 *
 * ASan 的行为：
 *   - ASan 在每个 malloc 块周围放置 redzone（不可访问的保护区）
 *   - 任何越界写入 redzone 的行为都会被 ASan 立即检测到
 *   - ASan 100% 拦截，绝不遗漏
 *
 * 编译（无检测）：
 *   gcc -Wall -g -O0 -o intermittent-bug intermittent-bug.c
 *
 * 编译（ASan）：
 *   gcc -fsanitize=address -g -O0 -o intermittent-bug-asan intermittent-bug.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(void) {
    /* 分配两个相邻的堆块 */
    char* buf = (char*)malloc(8);
    char* guard_block = (char*)malloc(16);

    if (!buf || !guard_block) {
        fprintf(stderr, "malloc failed\n");
        free(buf);
        free(guard_block);
        return 1;
    }

    /* 在 guard_block 中写入已知数据，方便观察是否被破坏 */
    strcpy(guard_block, "SAFE_GUARD");

    /* 模拟：从网络收到一个随机字节 */
    srand((unsigned int)time(NULL) ^ (unsigned int)(unsigned long)buf);
    unsigned char network_byte = (unsigned char)(rand() % 256);

    /*
     * BUG：向 8 字节的 buf 写入 9 个字节
     * 第 9 个字节（buf[8]）越界写到了 buf 的 malloc chunk 之外
     *
     * 堆布局（简化示意）：
     *   [chunk header: prev_size|size] [buf 8 bytes] [redzone or next chunk]
     *                                           buf[0..7]    buf[8] 写到这里 →
     *
     * 这个溢出的字节可能落在：
     *   1. next chunk 的 prev_size 字段 → 暂时无害（如果 next chunk 在使用中）
     *   2. next chunk 的 size 字段 → 可能破坏堆元数据
     *   3. next chunk 的用户数据 → 悄悄破坏别的数据
     */
    memset(buf, 'A', 8);
    buf[8] = (char)network_byte;  /* BUG! 第 9 个字节越界 */

    printf("overflow_byte = 0x%02x (%c)\n",
           network_byte,
           (network_byte >= 32 && network_byte < 127) ? network_byte : '.');

    /*
     * 释放两个块
     * 如果溢出破坏了堆元数据，free() 可能在这里崩溃
     * 但在 glibc tcache 中，小块 free 不检查相邻 chunk → 经常"幸存"
     */
    free(guard_block);
    free(buf);

    /*
     * 检查 guard_block 的数据是否被破坏（如果溢出方向刚好覆盖到）
     * 注意：guard_block 已释放，这里只是为了演示才访问
     * （在实际代码中这是 use-after-free，但这里仅作检测用）
     */
    printf("Survived this time! (但堆结构可能已被悄悄破坏)\n");
    return 0;
}
