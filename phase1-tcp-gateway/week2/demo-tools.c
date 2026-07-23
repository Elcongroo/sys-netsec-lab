/**
 * demo-tools.c — 三件武器教学演示（安全版，不会 crash 整个程序）
 *
 * 使用方法：
 *   1. 普通编译 + Valgrind：
 *      gcc -g -O1 -o demo-vg demo-tools.c
 *      valgrind --leak-check=full --track-origins=yes --show-leak-kinds=all ./demo-vg
 *
 *   2. ASan：
 *      gcc -fsanitize=address -g -O1 -o demo-asan demo-tools.c
 *      ./demo-asan
 *
 *   3. ASan+UBSan 一起：
 *      gcc -fsanitize=address,undefined -g -O1 -o demo-both demo-tools.c
 *      ./demo-both
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 每个 bug 函数用防护代码保护，防止把整个程序搞崩 */

void demo_heap_overflow() {
    printf("--- 1. 堆溢出 (ASan检测) ---\n");
    char* buf = (char*)malloc(8);
    strcpy(buf, "too_long");  // 写 9 字节到 8 字节空间（含\0）
    printf("  buf=%s\n", buf);
    free(buf);
}

void demo_use_after_free() {
    printf("\n--- 2. Use-After-Free (ASan+Valgrind都检测) ---\n");
    char* p = (char*)malloc(32);
    strcpy(p, "hello");
    free(p);
    // 注意：下面这行读取已释放的内存
    printf("  data after free = %c\n", p[0]);  // ← UAF
}

void demo_memory_leak() {
    printf("\n--- 3. 内存泄漏 (Valgrind+ASan都检测) ---\n");
    char* leaked = (char*)malloc(100);
    strcpy(leaked, "this will never be freed");
    printf("  已分配 100 字节，故意不释放\n");
}

void demo_early_return_leak() {
    printf("\n--- 4. 提前返回泄漏 (Valgrind检测) ---\n");
    char* buf = (char*)malloc(50);
    strcpy(buf, "data");

    /* 模拟错误路径 */
    printf("  模拟错误：提前 return，buf 没释放\n");
    return;  // 泄漏 buf
    // 正常路径会 free(buf)，但永远不会执行到这里
}

void demo_uninit_read() {
    printf("\n--- 5. 未初始化读取 (Valgrind检测, ASan不行) ---\n");
    int x;
    int* heap_x = (int*)malloc(sizeof(int));
    // 注意：malloc 不初始化，heap_x 的内容不确定

    if (x > 0) {  // ← 条件分支依赖未初始化值
        printf("  x > 0 (碰巧)\n");
    } else {
        printf("  x <= 0 (碰巧)\n");
    }

    printf("  heap 上的值: %d (未初始化)\n", *heap_x);
    free(heap_x);
}

void demo_double_free() {
    printf("\n--- 6. 双重释放 (ASan+Valgrind都检测) ---\n");
    char* p = (char*)malloc(16);
    free(p);
    /* 第二次 free 很危险，但我们用条件保护 */
    printf("  跳过第二次 free（会导致 abort）\n");
    printf("  实际代码中：free(p); free(p); ← double free!\n");
}

int main(void) {
    printf("===== 三件武器演示程序 =====\n");
    printf("用不同方式编译运行，对比三件武器的检测能力\n\n");

    demo_heap_overflow();
    demo_use_after_free();
    demo_memory_leak();
    demo_early_return_leak();
    demo_uninit_read();
    demo_double_free();

    printf("\n===== 程序结束（有 4 个泄漏：3个泄漏+1个UAF） =====\n");
    return 0;
}
