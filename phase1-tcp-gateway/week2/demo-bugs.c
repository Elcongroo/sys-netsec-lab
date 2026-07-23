/**
 * demo-bugs.c — 教学用的 Bug 演示程序
 *
 * 每个函数包含一类内存缺陷，用于学习 ASan / Valgrind / UBSan 的使用方法
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =================================================================
 * 缺陷 1：堆缓冲区溢出
 * 预期 ASan 报告：heap-buffer-overflow on address ...
 * 预期 Valgrind 报告：Invalid write of size 1
 * ================================================================= */
void bug1_heap_overflow() {
    printf("[BUG1] 堆溢出...\n");
    char* buf = (char*)malloc(8);
    strcpy(buf, "123456789ABCDEFG");  // 写了 17 字节，但只分配了 8 字节
    printf("  buf = %s\n", buf);
    free(buf);
}

/* =================================================================
 * 缺陷 2：栈缓冲区溢出
 * 预期 ASan 报告：stack-buffer-overflow
 * Valgrind 检测不到栈溢出！
 * ================================================================= */
void bug2_stack_overflow() {
    printf("[BUG2] 栈溢出...\n");
    char buf[8];
    strcpy(buf, "123456789ABCDEFG");  // 和上面一样的错误，但在栈上
    printf("  buf = %s\n", buf);
}

/* =================================================================
 * 缺陷 3：Use-After-Free
 * 预期 ASan 报告：heap-use-after-free
 * 预期 Valgrind 报告：Invalid read of size 1
 * ================================================================= */
void bug3_use_after_free() {
    printf("[BUG3] 释放后使用...\n");
    char* p = (char*)malloc(32);
    strcpy(p, "hello world");
    free(p);
    // p 现在是悬空指针！
    printf("  p after free = %s\n", p);  // 未定义行为
}

/* =================================================================
 * 缺陷 4：内存泄漏
 * 预期 Valgrind 报告：N bytes in 1 blocks are definitely lost
 * 预期 ASan 报告：检测到内存泄漏（进程退出时）
 * ================================================================= */
void bug4_memory_leak() {
    printf("[BUG4] 内存泄漏...\n");
    char* p = (char*)malloc(1024);
    strcpy(p, "leaked data");
    // 忘记 free(p)
    printf("  p = %s (忘记 free!)\n", p);
}

/* =================================================================
 * 缺陷 5：双重释放
 * 预期 ASan 报告：double-free
 * 预期 Valgrind 报告：Invalid free()
 * 预期 glibc：free(): double free detected in tcache 2
 * ================================================================= */
void bug5_double_free() {
    printf("[BUG5] 双重释放...\n");
    char* p = (char*)malloc(32);
    free(p);
    free(p);  // 第二次 free 同一块内存
}

/* =================================================================
 * 缺陷 6：未初始化内存读取（分支依赖）
 * 预期 Valgrind 报告：Conditional jump or move depends on uninitialised value
 * ASan 通常检测不到！
 * ================================================================= */
void bug6_uninit_read() {
    printf("[BUG6] 读取未初始化变量...\n");
    int x;  // 栈上变量，未初始化——值是不确定的
    if (x == 42) {  // 这个分支是否执行完全随机！
        printf("  神奇地命中了 42！\n");
    } else {
        printf("  x != 42 (x 的实际值是: %d)\n", x);
    }
}

/* =================================================================
 * 缺陷 7：整数溢出（UBSan 专门检测）
 * ASan 和 Valgrind 都检测不到！
 * ================================================================= */
void bug7_integer_overflow() {
    printf("[BUG7] 有符号整数溢出...\n");
    int a = 2147483647;  // INT_MAX
    printf("  INT_MAX = %d\n", a);
    printf("  INT_MAX + 1 = %d\n", a + 1);  // 有符号整数溢出 = 未定义行为
}

/* =================================================================
 * 缺陷 8：提前返回导致泄漏
 * 预期 Valgrind 报告：definitely lost
 * ================================================================= */
void bug8_early_return_leak(int should_fail) {
    printf("[BUG8] 提前返回泄漏...\n");
    char* buf = (char*)malloc(256);
    strcpy(buf, "important data");

    if (should_fail) {
        printf("  错误路径：直接返回，没有释放 buf！\n");
        return;  // ← 泄漏 buf
    }

    printf("  正常路径：buf = %s\n", buf);
    free(buf);
}

/* =================================================================
 * main：逐个测试
 * 为方便教学，每个 Bug 用条件编译控制是否执行
 * ================================================================= */
int main(void) {
    printf("=== 内存 Bug 演示程序 ===\n");
    printf("用 ASan 编译：gcc -fsanitize=address -g -O1 -o demo-asan demo-bugs.c\n");
    printf("用 UBSan 编译：gcc -fsanitize=undefined -g -O1 -o demo-ubsan demo-bugs.c\n");
    printf("普通编译用 Valgrind：gcc -g -O1 -o demo demo-bugs.c\n\n");

    // 全部执行；如果某个 Bug 会崩溃，单独注释掉测试
    bug1_heap_overflow();
    printf("\n");

    bug2_stack_overflow();
    printf("\n");

    bug3_use_after_free();
    printf("\n");

    bug4_memory_leak();
    printf("\n");

    // bug5_double_free();   // 取消注释前确保想测试它——它会 abort
    printf("[BUG5] 跳过（双重释放会直接 abort 后续代码不会执行）\n\n");

    bug6_uninit_read();
    printf("\n");

    bug7_integer_overflow();
    printf("\n");

    bug8_early_return_leak(1);  // 传入 1 触发错误路径
    printf("\n");

    printf("=== 程序结束 ===\n");
    return 0;
}
