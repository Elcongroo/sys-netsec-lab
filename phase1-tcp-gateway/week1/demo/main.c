/**
 * demo/main.c — 链接 libcalc 的演示程序
 *
 * 分别演示静态链接和动态链接两种方式。
 * 编译方式由 Makefile 控制。
 */

#include <stdio.h>
#include <stdlib.h>
#include "calc.h"

int main(void) {
    printf("=== libcalc 链接演示 ===\n");
    printf("库版本: %s\n\n", calc_version());

    /* 基本运算 */
    printf("calc_add(3, 4)      = %d\n", calc_add(3, 4));
    printf("calc_sub(10, 6)     = %d\n", calc_sub(10, 6));
    printf("calc_mul(7, 8)      = %d\n", calc_mul(7, 8));
    printf("calc_pow(2, 10)     = %d\n", calc_pow(2, 10));

    /* 除法（含错误处理） */
    int div_result;
    if (calc_div(100, 7, &div_result) == 0) {
        printf("calc_div(100, 7)    = %d\n", div_result);
    }
    /* 除零测试 */
    if (calc_div(5, 0, &div_result) != 0) {
        printf("calc_div(5, 0)      = ERROR (divide by zero)\n");
    }

    /* 内部状态 */
    printf("\n总运算次数: %d\n", calc_get_count());

    return 0;
}
