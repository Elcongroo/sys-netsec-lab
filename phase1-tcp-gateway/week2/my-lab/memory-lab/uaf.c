/**
 * use_after_free.c — Use-After-Free：两种危险场景
 *
 * 场景 1（基础版）：free 后继续读原指针
 * 场景 2（别名指针版）：两个指针指向同一块内存，一个 free 了，另一个还在用
 *
 * ASan 会直接报：heap-use-after-free
 *
 * 编译（错误版）：
 *   gcc -Wall -g -O0 -o uaf uaf.c
 *   gcc -fsanitize=address -g -O0 -o uaf-asan uaf.c
 *
 * 编译（修复版）：
 *   gcc -Wall -g -O0 -DFIX -o uaf-fixed uaf.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =================================================================
 * 演示 1：基础 UAF
 * 流程：malloc → 使用 → free → 继续使用（BUG!）
 *
 * 为什么危险：
 *   - free 后内存可能立刻被别的 malloc 拿走
 *   - 你读到的数据可能是别人的数据（信息泄露）
 *   - 你写入的数据可能破坏别人的结构（堆破坏）
 * ================================================================= */
void demo_basic_uaf(void) {
    printf("  [基础 UAF] ");

#ifndef FIX
    /* ---- 错误版本 ---- */
    char* p = (char*)malloc(32);
    strcpy(p, "sensitive_session_key_12345");
    printf("释放前: %s\n", p);

    free(p);  /* p 现在是悬空指针（dangling pointer） */

    /* BUG! 释放后还读 p */
    printf("释放后: %s  (悬空指针! 数据可能还在也可能不在)\n", p);

    /* 更危险的场景：分配新内存 */
    char* q = (char*)malloc(32);
    strcpy(q, "ATTACKER_DATA!!");
    printf("新分配后 p: %s  (p 指向的内存已被 q 复用!)\n", p);
    free(q);
#else
    /* ---- 修复版本 ---- */
    char* p = (char*)malloc(32);
    strcpy(p, "sensitive_session_key_12345");
    printf("释放前: %s\n", p);

    free(p);
    p = NULL;  /* 修复：free 后立即置 NULL */

    /* 如果后续代码错误地使用 p，至少会 segfault（容易发现）
     * 而不是悄悄读别人的数据（难以发现） */
    printf("释放后: 指针已置 NULL，不会误用\n");
#endif
}

/* =================================================================
 * 演示 2：别名指针 UAF（更隐蔽）
 * 问题：a 和 b 指向同一块内存，free(a) 之后还在用 b
 *
 * 这种场景在真实代码中很常见：
 *   - 一个链表节点被删除了，但另一个迭代器还指着它
 *   - 回调函数持有指针，主流程已经释放了
 * ================================================================= */
void demo_alias_uaf(void) {
    printf("  [别名 UAF] ");

#ifndef FIX
    /* ---- 错误版本 ---- */
    char* owner = (char*)malloc(32);
    strcpy(owner, "shared_data_block");

    char* alias = owner;  /* alias 和 owner 指向同一块内存 */

    printf("owner=%p alias=%p 数据=%s\n", (void*)owner, (void*)alias, alias);

    free(owner);  /* 释放了，但 alias 还指着这里 */

    /* BUG! 通过 alias 使用已释放的内存 */
    printf("free 后 alias 仍可读: %s (危险!)\n", alias);

    /* alias 没有被置 NULL，owner 也没有被置 NULL */
    /* 两个指针都变成了悬空指针 */
#else
    /* ---- 修复版本 ---- */
    char* owner = (char*)malloc(32);
    strcpy(owner, "shared_data_block");

    /* 修复：不要创建裸别名，或者使用引用计数 */
    /* 简单的做法：统一用 owner 管理，不暴露裸指针 */
    printf("owner=%p 数据=%s\n", (void*)owner, owner);

    free(owner);
    owner = NULL;
    /* 如果之前有别名，也应该置 NULL，但在 C 中很难追踪所有别名
     * 这就是为什么 C 中内存管理这么难——所有权不清晰 */
    printf("free 后 owner 已置 NULL\n");
#endif
}

/* =================================================================
 * 主流程
 * ================================================================= */
int main(void) {
    printf("=== Use-After-Free 演示 ===\n\n");

#ifdef FIX
    printf("--- 修复版本 ---\n");
#else
    printf("--- 错误版本（带 Bug）---\n");
#endif

    demo_basic_uaf();
    demo_alias_uaf();

    printf("\n=== 完成 ===\n");
    return 0;
}
