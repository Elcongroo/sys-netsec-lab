#include "calc.h"
#include <stddef.h>

/* 内部状态：全局计数器，跟踪所有运算调用次数 */
static int g_call_count = 0;

/* 内部辅助函数：不导出 */
static void bump(void) {
    g_call_count++;
}

int calc_get_count(void) {
    return g_call_count;
}

void calc_reset(void) {
    g_call_count = 0;
}

int calc_add(int a, int b) {
    bump();
    return a + b;
}

int calc_sub(int a, int b) {
    bump();
    return a - b;
}

int calc_mul(int a, int b) {
    bump();
    return a * b;
}

int calc_div(int a, int b, int *result) {
    bump();
    if (b == 0 || result == NULL) {
        return -1;
    }
    *result = a / b;
    return 0;
}

int calc_pow(int base, int exp) {
    bump();
    if (exp < 0) return -1;  /* 不支持负指数 */
    int r = 1;
    for (int i = 0; i < exp; i++) {
        r *= base;
    }
    return r;
}

const char* calc_version(void) {
    return "0.1.0";
}
