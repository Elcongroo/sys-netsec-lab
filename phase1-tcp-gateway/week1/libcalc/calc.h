#ifndef CALC_H
#define CALC_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * libcalc — 一个故意设计的示例计算库
 *
 * 包含运算、错误处理和内部状态，用于演示：
 *  - 静态库 (.a) 和动态库 (.so) 的构建
 *  - 符号可见性（哪些符号导出到符号表）
 *  - 头文件 API 设计
 */

/* 返回当前计算次数（内部状态示例） */
int calc_get_count(void);

/* 重置计算次数 */
void calc_reset(void);

/* 基本运算 */
int  calc_add(int a, int b);
int  calc_sub(int a, int b);
int  calc_mul(int a, int b);

/* 除法：返回 0 成功，-1 失败（除零） */
int  calc_div(int a, int b, int *result);

/* 幂运算（整数） */
int  calc_pow(int base, int exp);

/* 库版本字符串 */
const char* calc_version(void);

#ifdef __cplusplus
}
#endif

#endif /* CALC_H */
