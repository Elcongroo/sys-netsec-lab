/**
 * demo-ubsan-only.c — 专门演示 UBSan 能检测的未定义行为
 *
 * 编译：gcc -fsanitize=undefined -g -O1 -o demo-ubsan demo-ubsan-only.c
 * 运行：./demo-ubsan
 */

#include <stdio.h>
#include <limits.h>

void test_signed_overflow() {
    printf("[UBSan] 有符号整数溢出（UB）:\n");
    int a = INT_MAX;
    printf("  INT_MAX = %d\n", a);
    int b = a + 1;  // ← 有符号溢出 = 未定义行为，UBSan 会报！
    printf("  INT_MAX + 1 = %d\n", b);
}

void test_unsigned_wraparound() {
    printf("\n[UBSan] 无符号回绕（合法）:\n");
    unsigned int a = 4294967295U;  // UINT_MAX
    printf("  UINT_MAX = %u\n", a);
    unsigned int b = a + 1;  // ← 无符号回绕是定义好的行为，UBSan 不管
    printf("  UINT_MAX + 1 = %u  （UBSan 不会报，因为这是合法的）\n", b);
}

void test_shift_overflow() {
    printf("\n[UBSan] 移位溢出:\n");
    int x = 1;
    int y = x << 31;  // 1<<31 在有符号 32 位 int 里溢出
    printf("  1 << 31 = %d\n", y);
}

void test_div_by_zero() {
    printf("\n[UBSan] 除零:\n");
    int a = 42;
    int b = 0;
    // int c = a / b;   // ← 取消注释会 crash，UBSan 会报
    printf("  跳过除零（会导致 SIGFPE）\n");
}

void test_null_deref() {
    printf("\n[UBSan] NULL 指针解引用:\n");
    int* p = NULL;
    // *p = 42;   // ← 取消注释会 crash，UBSan 会报
    printf("  跳过 NULL 解引用（会导致 SIGSEGV）\n");
}

int main(void) {
    printf("=== UBSan 演示：未定义行为检测 ===\n\n");
    printf("UBSan 检测的是 C 标准中的\"未定义行为\"，不是所有内存错误\n");
    printf("（内存错误用 ASan/Valgrind，未定义行为用 UBSan）\n\n");

    test_signed_overflow();
    test_unsigned_wraparound();
    test_shift_overflow();
    test_div_by_zero();
    test_null_deref();

    printf("\n=== 完成 ===\n");
    return 0;
}
