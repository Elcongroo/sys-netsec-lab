# memory-lab — 五类内存缺陷演示实验室

> 每个文件包含"错误版本"和"修复版本"（通过 `-DFIX` 切换）

## 快速开始

```bash
# 查看所有 target
make

# === 错误版本（带 Bug） ===

# 编译所有普通版本
make all

# 用 ASan 运行所有检查
make test-asan

# 用 Valgrind 运行所有检查
make test-valgrind

# 用 UBSan 运行所有检查
make test-ubsan

# === 修复版本（Bug 已修复） ===

# 编译所有修复版本
make fixed

# 用 Valgrind 验证修复版本（应全通过）
make test-fixed-valgrind

# 清理
make clean
```

## 文件说明

| 文件 | 缺陷类型 | 包含场景 | ASan 能检 | Valgrind 能检 |
|------|---------|---------|----------|-------------|
| `overflow.c` | 缓冲区溢出 | 栈溢出 / 堆溢出 / off-by-one | ✅ | ✅ |
| `uaf.c` | Use-After-Free | 基础 UAF / 别名指针 UAF | ✅ | ✅ |
| `memory_leak.c` | 内存泄漏 | 循环泄漏 / 提前返回泄漏 | ✅ | ✅ (更详细) |
| `uninit.c` | 未初始化读取 | 栈未初始化 / 堆 malloc vs calloc | 部分 | ✅ (最擅长) |
| `double_free.c` | 双重释放 | 简单 double free / 别名 double free | ✅ | ✅ |

## 五类缺陷速查

| # | 类型 | 发生了什么 | 一定会崩？ | 首选工具 |
|---|------|-----------|-----------|---------|
| 1 | 缓冲区溢出 | 写到了分配范围外面 | 不一定 | ASan |
| 2 | Use-After-Free | free 之后还在用那根指针 | 不一定 | ASan |
| 3 | 内存泄漏 | malloc 之后没 free | 不会崩，但内存耗尽 | Valgrind |
| 4 | 未初始化 | 读了没被赋值过的变量 | 不一定 | Valgrind |
| 5 | 双重释放 | 对同一内存 free 两次 | 通常会 | ASan |

## 编译切换（BUG / FIX）

每个 `.c` 文件都用同一个宏控制：

```bash
# 错误版本（默认）
gcc -o overflow overflow.c

# 修复版本
gcc -DFIX -o overflow-fixed overflow.c
```

在代码中：
```c
#ifndef FIX
    /* 这里是有 Bug 的代码 */
#else
    /* 这里是修复后的代码 */
#endif
```

## 预期输出

### ASan（`make test-asan`）
所有 5 个程序应该都 **FAIL** — ASan 检测到了内存错误。这是**预期行为**，证明 ASan 有效。

### Valgrind（`make test-valgrind`）
所有 5 个程序应该都 **FAIL** — Valgrind 检测到了内存错误。

### 修复版 Valgrind（`make test-fixed-valgrind`）
所有 5 个程序应该都 **PASS** — 修复后没有任何错误。
