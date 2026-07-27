/**
 * uninit.c — 未初始化内存读取：栈上未初始化 + 堆上未初始化
 *
 * Valgrind 会精确追踪每个字节的初始化状态。
 * ASan 在某些优化级别下也能检测（-O1 以上配合 -fsanitize=memory 更好）。
 *
 * 编译：
 *   gcc -Wall -g -O0 -o uninit uninit.c
 *
 * 检测（Valgrind）：
 *   valgrind --track-origins=yes ./uninit
 *
 * 编译（修复版）：
 *   gcc -Wall -g -O0 -DFIX -o uninit-fixed uninit.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =================================================================
 * 演示 1：栈上未初始化变量
 * 问题：局部变量声明了但没有赋值就使用
 * Valgrind 报：Conditional jump or move depends on uninitialised value(s)
 * ================================================================= */
void demo_stack_uninit(void) {
    printf("  [栈未初始化] ");

#ifndef FIX
    /* ---- 错误版本 ---- */
    int decision;       /* 声明了但没有初始化 */
    char name[32];      /* 同样未初始化 */

    /* BUG! 读取未初始化的值来做决定 */
    if (decision > 0) {
        printf("decision=%d (这可能是任何值!)\n", decision);
    } else if (decision == 0) {
        printf("decision=%d (碰巧是0? 还是真的是0?)\n", decision);
    } else {
        printf("decision=%d (垃圾值，每次运行都可能不同)\n", decision);
    }

    /* name 里也是垃圾值 */
    printf("  name 前 4 字节可能是垃圾: %02x %02x %02x %02x\n",
           (unsigned char)name[0], (unsigned char)name[1],
           (unsigned char)name[2], (unsigned char)name[3]);
#else
    /* ---- 修复版本 ---- */
    int decision = 0;       /* 修复：声明时初始化 */
    char name[32] = {0};    /* 修复：初始化为全零 */

    if (decision > 0) {
        printf("decision=%d\n", decision);
    } else {
        printf("decision=%d (确定性: 0)\n", decision);
    }

    printf("  name 前 4 字节: %02x %02x %02x %02x (全零)\n",
           (unsigned char)name[0], (unsigned char)name[1],
           (unsigned char)name[2], (unsigned char)name[3]);
#endif
}

/* =================================================================
 * 演示 2：堆上未初始化（malloc vs calloc）
 * 问题：malloc 不保证内存为零，读了没写过的堆内存
 * Valgrind 报：Use of uninitialised value of size N
 *             Uninitialised value was created by a heap allocation
 * ================================================================= */
void demo_heap_uninit(void) {
    printf("  [堆未初始化] ");

#ifndef FIX
    /* ---- 错误版本 ---- */
    int* data = (int*)malloc(4 * sizeof(int));  /* 分配 4 个 int */
    /* BUG! 没有初始化就直接使用 */

    printf("data[0]=%d data[1]=%d data[2]=%d data[3]=%d (全是垃圾值!)\n",
           data[0], data[1], data[2], data[3]);

    /* 更隐蔽的场景：部分初始化 */
    struct {
        int id;
        char* name;
        double score;
    }* rec = (void*)malloc(sizeof(*rec));

    rec->id = 42;    /* 只初始化了 id */
    /* name 和 score 没有初始化！ */

    if (rec->name != NULL) {   /* BUG! 读到垃圾值做判断 */
        printf("  name 不为 NULL? 其实是垃圾指针: %p\n", (void*)rec->name);
    }

    free(data);
    free(rec);
#else
    /* ---- 修复版本 ---- */
    /* 方法 1：用 calloc（分配时自动清零） */
    int* data = (int*)calloc(4, sizeof(int));
    printf("calloc: data[0]=%d data[1]=%d data[2]=%d data[3]=%d (全零)\n",
           data[0], data[1], data[2], data[3]);

    /* 方法 2：malloc + memset */
    struct {
        int id;
        char* name;
        double score;
    }* rec = (void*)malloc(sizeof(*rec));
    if (rec) {
        memset(rec, 0, sizeof(*rec));  /* 显式清零 */
        rec->id = 42;
    }

    if (rec->name != NULL) {
        printf("  name 是 NULL? %s (确定性: NULL)\n",
               rec->name == NULL ? "是" : "否");
    }

    free(data);
    free(rec);
#endif
}

/* =================================================================
 * 主流程
 * ================================================================= */
int main(void) {
    printf("=== 未初始化内存读取演示 ===\n\n");

#ifdef FIX
    printf("--- 修复版本 ---\n");
#else
    printf("--- 错误版本（带 Bug）---\n");
#endif

    demo_stack_uninit();
    demo_heap_uninit();

    printf("\n=== 完成 ===\n");
    return 0;
}
