# memory-lab — 五类内存缺陷实验笔记

> 日期：2026-07-24  
> 验证：ASan + Valgrind + 修复版全通过

---

## 实验结果一览

| 缺陷类型 | 文件 | ASan (错误版) | Valgrind (错误版) | Valgrind (修复版) |
|---------|------|:---:|:---:|:---:|
| 缓冲区溢出 | `overflow.c` | ❌ | ❌ | ✅ |
| Use-After-Free | `uaf.c` | ❌ | ❌ | ✅ |
| 内存泄漏 | `memory-leak.c` | ❌ | ❌ | ✅ |
| 未初始化读取 | `uninit.c` | ✅ (测不到) | ❌ | ✅ |
| 双重释放 | `double-free.c` | ❌ | ❌ | ✅ |

说明：❌ = 检测到 Bug（预期行为），✅ = 无错误

---

## 工具能力对比

| 检测能力 | ASan | Valgrind | UBSan |
|---------|:---:|:---:|:---:|
| 堆溢出 | ✅ | ✅ | — |
| 栈溢出 | ✅ | ✅ | — |
| Use-After-Free | ✅ | ✅ | — |
| Double Free | ✅ | ✅ | — |
| 内存泄漏 | ✅ | ✅⭐ | — |
| 未初始化读取 | — | ✅⭐ | — |
| 未定义行为 | — | — | ✅⭐ |

⭐ 表示该工具的**最强项**

**关键认知**：没有一种工具能检测所有缺陷。ASan 快但查不了未初始化读，Valgrind 全但不能和 ASan 同时用，UBSan 专注于未定义行为。三件武器配合使用才是完整防线。

---

## 每类缺陷的核心认识

### 1. 缓冲区溢出
- **栈溢出**：覆盖相邻栈变量 → 可能覆盖返回地址 → SIGSEGV
- **堆溢出**：覆盖 chunk 元数据 → free 时 crash（"corrupted double-linked list"）
- **off-by-one**：最难发现，因为溢出量太小，可能落在对齐填充上

### 2. Use-After-Free
- **基础 UAF**：free 后直接读 → 数据可能还在（堆没被覆盖）也可能不在了（被复用）
- **别名 UAF**：多个指针指向同一内存 → 所有权不清晰 → 最大的安全漏洞来源
- 修复核心：free 后立即置 NULL

### 3. 内存泄漏
- **循环泄漏**：每次迭代分配不释放 → 内存持续增长
- **提前返回泄漏**：错误路径忘了清理 → goto cleanup 模式是最佳方案
- 修复核心：goto cleanup 统一释放，`free(NULL)` 是安全的

### 4. 未初始化内存
- **栈未初始化**：局部变量声明了没赋值 → 值是"天知道"
- **堆未初始化**：malloc vs calloc → calloc 自动清零但更慢
- Valgrind 能追踪每个字节的"是否被写过"状态，这是它最独特的能力

### 5. 双重释放
- **直接重复**：同一函数内两次 free(p)
- **别名重复**：a 和 b 指向同一块，a free 了 b 又 free
- glibc 自带检测：通常会直接 abort 并打印 "double free or corruption"

---

## 快速命令

```bash
# 进入 lab 目录
cd my-lab/memory-lab

# 编译所有错误版本
make all

# 用 ASan 检查（4/5 能抓到，除 uninit）
make test-asan

# 用 Valgrind 检查（5/5 全抓到）
make test-valgrind

# 编译所有修复版本
make fixed

# 验证修复版本（应该全 PASS）
make test-fixed-valgrind

# 清理
make clean
```
