/**
 * double_free.c — 双重释放
 *
 * 对同一块内存调用 free() 两次，破坏堆的内部数据结构。
 * glibc 会直接 abort() 并打印 "double free or corruption"。
 * ASan 会报：attempting double-free
 *
 * 编译：
 *   gcc -Wall -g -O0 -o double-free double_free.c
 *   gcc -fsanitize=address -g -O0 -o double-free-asan double_free.c
 *
 * 修复版：
 *   gcc -Wall -g -O0 -DFIX -o double-free-fixed double_free.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =================================================================
 * 演示 1：同一个函数内两次 free
 * 看似低级，但在复杂函数中很容易发生：
 *   - 函数开头分配 → 中途 free → 末尾又 free
 *   - 两个不同分支都在各自 free，但代码合并后重复
 * ================================================================= */
void demo_simple_double_free(void) {
    printf("  [简单双重释放] ");

#ifndef FIX
    /* ---- 错误版本 ---- */
    char* p = (char*)malloc(64);
    if (!p) return;
    strcpy(p, "important stuff");

    free(p);        /* 第一次释放 */
    /* ... 很多行代码 ... */
    free(p);        /* BUG! 第二次释放同一个指针 */
#else
    /* ---- 修复版本 ---- */
    char* p = (char*)malloc(64);
    if (!p) return;
    strcpy(p, "important stuff");

    free(p);
    p = NULL;       /* 修复：free 后立即置 NULL */

    /* free(NULL) 是安全的，什么也不做 */
    free(p);        /* 安全 */
#endif
}

/* =================================================================
 * 演示 2：别名导致的"间接"双重释放
 * 两个指针指向同一块内存，两个都 free 了
 * 这在复杂数据结构的"清理"函数中很常见
 * ================================================================= */
void demo_alias_double_free(void) {
    printf("  [别名双重释放] ");

#ifndef FIX
    /* ---- 错误版本 ---- */
    char* a = (char*)malloc(32);
    char* b = a;    /* b 是 a 的别名 */

    strcpy(a, "shared");

    free(a);        /* 释放 a 指向的内存 */
    /* a 和 b 都是悬空指针了 */

    free(b);        /* BUG! b 还指向同一块已释放的内存 → double free */
#else
    /* ---- 修复版本 ---- */
    char* a = (char*)malloc(32);
    char* b = a;    /* b 是 a 的别名 */

    strcpy(a, "shared");

    free(a);         /* 释放内存 */
    a = NULL;        /* 所有者置 NULL */

    /* b 仍然是悬空指针！这是 C 语言中无法自动解决的问题 */
    /* 最佳实践：用 a 作为唯一"所有者"，不让 b 拥有释放权 */
    if (b != NULL) {
        printf("b 还是悬空的，但至少我们知道它不应该被 free\n");
    }
    /* 注意：在真实项目中需要明确"所有权"——谁分配、谁释放 */
#endif
}

/* =================================================================
 * 主流程
 * ================================================================= */
int main(void) {
    printf("=== 双重释放演示 ===\n\n");

#ifdef FIX
    printf("--- 修复版本 ---\n");
#else
    printf("--- 错误版本（带 Bug）---\n");
#endif

    /* demo_simple_double_free 在错误版会 crash，
     * 所以这里先跑，如果它 crash 了后面就不会跑 */
    demo_simple_double_free();

#ifndef FIX
    printf("  (如果看到这行说明第一次 double free 没 crash...)\n");
#else
    demo_alias_double_free();
#endif

    printf("\n=== 完成 ===\n");
    return 0;
}
