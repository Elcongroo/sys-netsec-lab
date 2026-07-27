/**
 * overflow.c — 缓冲区溢出三连：栈溢出 / 堆溢出 / off-by-one
 *
 * 每类缺陷都有"错误版本"和"修复版本"。
 * 编译时加 -DFIX 使用修复版本，不加则使用错误版本。
 *
 * 编译（错误版）：
 *   gcc -Wall -g -O0 -o overflow overflow.c
 *   gcc -fsanitize=address -g -O0 -o overflow-asan overflow.c
 *
 * 编译（修复版）：
 *   gcc -Wall -g -O0 -DFIX -o overflow-fixed overflow.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =================================================================
 * 演示 1：栈缓冲区溢出
 * 问题：char[8] 装不下 "this_string_is_way_too_long"
 * ASan 报：stack-buffer-overflow
 * ================================================================= */
void demo_stack_overflow(void) {
    printf("  [栈溢出] ");

#ifndef FIX
    /* ---- 错误版本 ---- */
    char buf[8];
    strcpy(buf, "12345678901234");  /* BUG! 14 字节写到 8 字节 buf */
    printf("buf = %s  (写了 14 字节到 8 字节的 buf)\n", buf);
#else
    /* ---- 修复版本 ---- */
    const char* src = "12345678901234";
    size_t len = strlen(src);
    char* buf = (char*)malloc(len + 1);
    if (buf) {
        strcpy(buf, src);
        printf("buf = %s  (动态分配了 %zu 字节)\n", buf, len + 1);
        free(buf);
    }
#endif
}

/* =================================================================
 * 演示 2：堆缓冲区溢出
 * 问题：malloc(8) 后拷贝了 20 字节的字符串
 * ASan 报：heap-buffer-overflow
 * ================================================================= */
void demo_heap_overflow(void) {
    printf("  [堆溢出] ");

#ifndef FIX
    /* ---- 错误版本 ---- */
    char* buf = (char*)malloc(8);
    strcpy(buf, "this_is_a_heap_overflow_test");  /* BUG! 写穿堆缓冲区 */
    printf("buf = %s\n", buf);
    free(buf);  /* 注意：溢出可能已破坏堆元数据，free 可能 crash */
#else
    /* ---- 修复版本 ---- */
    const char* src = "this_is_a_heap_overflow_test";
    size_t len = strlen(src);
    char* buf = (char*)malloc(len + 1);
    if (buf) {
        strcpy(buf, src);
        printf("buf = %s\n", buf);
        free(buf);
    }
#endif
}

/* =================================================================
 * 演示 3：Off-by-One（最隐蔽的溢出）
 * 问题：循环条件 i <= 8，向 buf[8] 写了数据（buf 只有 0..7）
 * ASan 报：stack-buffer-overflow（通常能检测到）
 * 为什么危险：如果 buf[8] 恰好落在对齐填充字节上，不会 crash
 *           但 buf[8] 可能正好是栈保护 canary 的第一个字节！
 * ================================================================= */
void demo_off_by_one(void) {
    printf("  [Off-by-One] ");
    char buf[8];
    int result = 0;

#ifndef FIX
    /* ---- 错误版本 ---- */
    for (int i = 0; i <= 8; i++) {  /* BUG! 应该是 i < 8 */
        buf[i] = 'A';
    }
    result = 1;
#else
    /* ---- 修复版本 ---- */
    for (int i = 0; i < 8; i++) {   /* 正确：i < 8 */
        buf[i] = 'A';
    }
    result = 1;
#endif
    /* 确保 buf 被"使用"了，避免编译器警告 */
    printf("done (result=%d, buf[0]='%c')\n", result, (char)buf[0]);
}

/* =================================================================
 * 主流程：依次执行三个演示
 * ================================================================= */
int main(void) {
    printf("=== 缓冲区溢出演示 ===\n\n");

#ifdef FIX
    printf("--- 修复版本 ---\n");
#else
    printf("--- 错误版本（带 Bug）---\n");
#endif

    demo_stack_overflow();
    demo_heap_overflow();
    demo_off_by_one();

    printf("\n=== 完成 ===\n");
    return 0;
}
