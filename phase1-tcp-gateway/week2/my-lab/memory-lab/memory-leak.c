/**
 * memory_leak.c — 内存泄漏：两种经典场景
 *
 * Valgrind 会精确告诉你：哪一行 malloc 的、多少字节、丢了
 *
 * 编译：
 *   gcc -Wall -g -O0 -o memory-leak memory_leak.c
 *
 * 检测（Valgrind）：
 *   valgrind --leak-check=full --show-leak-kinds=all ./memory-leak
 *
 * 检测（ASan）：
 *   gcc -fsanitize=address -g -O0 -o memory-leak-asan memory_leak.c
 *   ASAN_OPTIONS=detect_leaks=1 ./memory-leak-asan
 *
 * 编译（修复版）：
 *   gcc -Wall -g -O0 -DFIX -o memory-leak-fixed memory_leak.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LEAK_COUNT 5

/* =================================================================
 * 演示 1：循环中忘记释放（最经典的泄漏场景）
 * 每次循环 malloc 但不 free，内存持续增长
 *
 * Valgrind 输出示例：
 *   X bytes in N blocks are definitely lost
 *     at malloc ... demo_loop_leak (memory_leak.c:XX)
 * ================================================================= */
void demo_loop_leak(void) {
    printf("  [循环泄漏] ");

#ifndef FIX
    /* ---- 错误版本 ---- */
    for (int i = 0; i < LEAK_COUNT; i++) {
        char* buf = (char*)malloc(128);
        if (buf) {
            snprintf(buf, 128, "packet_data_#%d", i);
            /* 处理 buf ... */
            printf("处理: %s\n", buf);
        }
        /* BUG! 忘记 free(buf); 每次泄漏 128 字节 */
        /* 如果这是一个服务器中处理请求的代码，内存会持续增长 */
    }
    printf("泄漏了 %d x 128 = %d 字节\n", LEAK_COUNT, LEAK_COUNT * 128);
#else
    /* ---- 修复版本 ---- */
    for (int i = 0; i < LEAK_COUNT; i++) {
        char* buf = (char*)malloc(128);
        if (buf) {
            snprintf(buf, 128, "packet_data_#%d", i);
            printf("处理: %s\n", buf);
            free(buf);  /* 修复：用完后释放 */
        }
    }
    printf("已释放所有 buffer\n");
#endif
}

/* =================================================================
 * 演示 2：错误路径上的泄漏（goto cleanup 的反面教材）
 * 正常路径释放了，但出错路径没有——这是最常见的泄漏
 *
 * 真实场景：打开文件、分配 buffer、解析失败 → 直接 return -1 → 泄漏
 * ================================================================= */
void demo_early_return_leak(void) {
    printf("  [提前返回泄漏] ");

#ifndef FIX
    /* ---- 错误版本 ---- */
    char* config = (char*)malloc(256);
    char* buffer = (char*)malloc(1024);

    if (!config || !buffer) {
        /* 如果第二个 malloc 失败，第一个 config 没有被释放 */
        return;
    }

    strcpy(config, "some_config_data");

    /* 模拟：处理过程中发现数据损坏 */
    if (config[0] == 's') {  /* 总是触发 */
        /* BUG! 直接 return，config 和 buffer 都泄漏了 */
        printf("数据损坏，提前返回 (泄漏 config + buffer)\n");
        return;
    }

    /* 正常路径（永远不会执行到） */
    free(config);
    free(buffer);
#else
    /* ---- 修复版本：goto cleanup 模式 ---- */
    char* config = NULL;
    char* buffer = NULL;

    config = (char*)malloc(256);
    buffer = (char*)malloc(1024);

    if (!config || !buffer) {
        goto cleanup;
    }

    strcpy(config, "some_config_data");

    if (config[0] == 's') {
        printf("数据损坏，但通过 goto cleanup 释放资源\n");
        goto cleanup;  /* 修复：跳转到统一的清理点 */
    }

cleanup:
    free(config);
    free(buffer);
    /* free(NULL) 是安全的，所以即使 malloc 失败也没问题 */
#endif
}

/* =================================================================
 * 主流程
 * ================================================================= */
int main(void) {
    printf("=== 内存泄漏演示 ===\n\n");

#ifdef FIX
    printf("--- 修复版本 ---\n");
#else
    printf("--- 错误版本（带 Bug）---\n");
#endif

    demo_loop_leak();
    demo_early_return_leak();

    printf("\n=== 完成 ===\n");
    return 0;
}
