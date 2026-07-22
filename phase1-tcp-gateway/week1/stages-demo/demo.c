/**
 * stages-demo/demo.c — 编译四阶段演示文件
 *
 * 这个文件故意包含宏、注释和多个函数，
 * 让你在每一阶段都能观察到有意义的输出变化。
 */
#include <stdio.h>
#include <stdlib.h>

/* 宏：会在预处理阶段展开 */
#define MSG_PREFIX  "[demo]"
#define ARRAY_SIZE  5
#define MAX(a, b)   ((a) > (b) ? (a) : (b))

/* 全局变量：会在 .data 段 */
static int g_counter = 0;

/* 内部函数：static 限制了外部可见性 */
static void increment(void) {
    g_counter++;
}

/**
 * 计算数组元素之和
 * 包含一个有意的边界假设：len >= 0
 */
int sum_array(const int *arr, int len) {
    int total = 0;
    for (int i = 0; i < len; i++) {
        total += arr[i];
    }
    increment();
    return total;
}

/**
 * 主函数：演示宏展开、函数调用、条件编译
 */
int main(void) {
    int data[ARRAY_SIZE] = {1, 2, 3, 4, 5};

    printf("%s array_sum=%d\n", MSG_PREFIX, sum_array(data, ARRAY_SIZE));
    printf("%s max=%d\n", MSG_PREFIX, MAX(data[0], data[4]));
    printf("%s counter=%d\n", MSG_PREFIX, g_counter);

    return 0;
}
