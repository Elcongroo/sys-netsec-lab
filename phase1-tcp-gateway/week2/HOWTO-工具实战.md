# W2 工具实战：ASan / Valgrind / UBSan 从零上手

**原则**：每个命令你亲手敲，我解释它的输出每一行在说什么。

---

## 准备工作：创建你的实验目录

```bash
cd ~/projects/upup/sys-netsec-lab/phase1-tcp-gateway/week2
mkdir -p my-lab
cd my-lab
```

后面所有操作都在 `my-lab/` 里进行。

---

## 第一步：写一个最简单的 Bug 程序

用你喜欢的编辑器创建 `step1.c`：

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    char* buf = (char*)malloc(8);   // 只分配了 8 字节
    strcpy(buf, "this string is too long");  // 写了远超 8 字节的内容
    printf("buf = %s\n", buf);
    free(buf);
    return 0;
}
```

这个程序的 Bug 是什么？**malloc 分配了 8 字节，但 strcpy 写入了远超 8 字节的内容**。

---

## 第二步：先不用任何工具，直接跑

```bash
gcc -Wall -Wextra -g -o step1 step1.c
./step1
```

**你看到了什么？** 把这个输出记下来。

> 提示：新版 gcc 在编译时就会警告你溢出。注意看编译输出里的 warning。

---

## 第三步：用 ASan 跑

ASan 的原理：编译时在代码里插入"检查点"。每次读写内存前，先检查地址是否合法。

```bash
# 编译（加 -fsanitize=address）
gcc -fsanitize=address -g -O1 -o step1-asan step1.c

# 运行
./step1-asan
```

**输出解读**：ASan 会打印类似这样的报告（你的地址会不同）：

```
=================================================================
==12345==ERROR: AddressSanitizer: heap-buffer-overflow
WRITE of size 25 at 0x... thread T0
    #0 ... in strcpy
    #1 ... in main step1.c:6      ← 错误发生在 step1.c 第 6 行

0x... is located 0 bytes after 8-byte region [...)
allocated by thread T0 here:
    #0 ... in malloc
    #1 ... in main step1.c:5      ← 这块内存是在 step1.c 第 5 行分配的
```

**逐行解释**：
- `heap-buffer-overflow` = 堆缓冲区溢出（你写到 malloc 的范围外面了）
- `WRITE of size 25` = 你试图写 25 字节
- `step1.c:6` = 出错的那行代码
- `0 bytes after 8-byte region` = 你写了 8 字节之后紧接着的地方（越界了 0 字节的间距）
- `allocated ... step1.c:5` = 越界的内存是在第 5 行分配的

**学会读 ASan 报告就等于会了 80% 的调试工作。**

---

## 第四步：用 Valgrind 跑

Valgrind 不需要重新编译。它模拟 CPU 执行，追踪每一块内存的状态。

```bash
# 用刚才普通编译的版本就行
valgrind --leak-check=full ./step1
```

**Valgrind 的输出会说**：

```
==12345== Invalid write of size 1
==12345==    at ... main (step1.c:6)
==12345==  Address 0x... is 0 bytes after a block of size 8 alloc'd
==12345==    at ... malloc
==12345==    by ... main (step1.c:5)
```

**理解**：
- `Invalid write of size 1` = 写到不该写的地方了
- `0 bytes after a block of size 8 alloc'd` = 在 8 字节块后面越界写入
- `step1.c:6` = 出错位置
- `step1.c:5` = 分配位置

---

## 第五步：对比两种工具的输出

| | ASan | Valgrind |
|------|------|------|
| 需要重新编译？ | 是（`-fsanitize=address`） | 否 |
| 运行速度 | 快（~2x 慢） | 慢（~10-50x 慢） |
| 报错格式 | `heap-buffer-overflow on address` | `Invalid write/read of size N` |
| 共同点 | 都告诉你：错误类型 + 出错行号 + 分配行号 |

**实战经验**：开发时用 ASan（快），提交前用 Valgrind（全面）。

---

## 现在轮到你动手

按这个顺序，在 `my-lab/` 目录下：

### 练习 1：堆溢出
- 创建 `ex1-heap-overflow.c`，写一个堆溢出的程序
- 分别用 ASan 和 Valgrind 跑
- 把两次的输出截图或复制下来，标注"ASan 输出"和"Valgrind 输出"
- 对比：两个工具报的错误信息有什么相同和不同？

### 练习 2：Use-After-Free
- 创建 `ex2-uaf.c`：malloc → free → 再使用
- 用 ASan 跑，看报告
- 用 Valgrind 跑，看报告
- 问题：ASan 的输出里，"freed by thread T0 here" 是什么意思？

### 练习 3：内存泄漏
- 创建 `ex3-leak.c`：故意 malloc 后不 free
- 用 Valgrind `--leak-check=full` 跑
- 看懂 `definitely lost` 是什么意思
- 再加一个"提前 return 导致泄漏"的场景

### 练习 4：未初始化读取
- 创建 `ex4-uninit.c`：声明一个 int 不赋值，然后 if (x > 0) 分支
- 用 Valgrind `--track-origins=yes` 跑
- 看懂 `Conditional jump depends on uninitialised value` 的含义

### 练习 5：双重释放
- 创建 `ex5-double-free.c`：对同一指针 free 两次
- 用 ASan 跑
- 用 Valgrind 跑
- 观察：哪个工具的报告更清晰？

---

## 速查卡（打出来贴显示器旁边）

```bash
# ASan — 开发时每次编译都加上
gcc -fsanitize=address -g -O1 xxx.c -o xxx
./xxx
# 报错关键词：heap-buffer-overflow / heap-use-after-free / double-free

# Valgrind — 提交前跑一次
gcc -g -O1 xxx.c -o xxx
valgrind --leak-check=full --track-origins=yes ./xxx
# 报错关键词：Invalid write/read / Conditional jump depends on uninitialised / definitely lost

# UBSan — 怀疑整数溢出时加上
gcc -fsanitize=undefined -g -O1 xxx.c -o xxx
./xxx
# 报错关键词：signed integer overflow / left shift cannot be represented
```

---

## 遇到看不懂的错误信息怎么办？

把错误信息**完整复制**发给我，我会逐行解释。

**这是工程师的核心能力之一：看工具输出，理解它在说什么。**

---

> 完成全部 5 个练习后告诉我，然后我们进入 broken-server.c 实战修复。
