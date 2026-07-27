# 五种内存缺陷 — 工具输出速查

> 笔记目的：知道每种 Bug 在 ASan / Valgrind / UBSan 下分别报什么关键词。

---

## 1. 堆溢出（Heap Buffer Overflow）

**代码特征**：malloc(N) 后写入超过 N 字节

**ASan 输出关键词**：
```
ERROR: AddressSanitizer: heap-buffer-overflow
WRITE of size XX at 0x... thread T0
    #0 ... in main step1.c:6        ← 溢出发生的行
0x... is located 0 bytes after 8-byte region
allocated by thread T0 here:
    #0 ... in malloc
    #1 ... in main step1.c:5         ← 分配发生的行
```

**Valgrind 输出关键词**：
```
Invalid write of size 1
   at ... main (xxx.c:6)
 Address 0x... is 0 bytes after a block of size 8 alloc'd
   at ... malloc
   by ... main (xxx.c:5)
```

**对比**：ASan 更快更详细；Valgrind 不需要重新编译。栈溢出 ASan 能检测（`stack-buffer-overflow`），Valgrind 不能。

---

## 2. Use-After-Free (UAF)

**代码特征**：free(p) 之后再读/写 *p

**ASan 输出关键词**：
```
ERROR: AddressSanitizer: heap-use-after-free
READ of size XX at 0x... thread T0
    #0 ... in main xxx.c:7           ← 使用已释放内存的行
freed by thread T0 here:
    #0 ... in free
    #1 ... in main xxx.c:6           ← free 发生的行
previously allocated by thread T0 here:
    #0 ... in malloc
    #1 ... in main xxx.c:5           ← 分配发生的行
```

**Valgrind 输出关键词**：
```
Invalid read of size 1
   at ... main (xxx.c:7)
 Address 0x... is 0 bytes inside a block of size 32 free'd
   at ... free
   by ... main (xxx.c:6)
```

**关键区别**：ASan 明确说 "heap-use-after-free"；Valgrind 说 "Invalid read/write ... inside a block free'd"。

---

## 3. 内存泄漏（Memory Leak）

**代码特征**：malloc 后没有对应的 free（包括提前 return 路径）

**ASan**：进程退出时打印泄漏汇总（需环境变量 `ASAN_OPTIONS=detect_leaks=1`，默认开启）
```
SUMMARY: AddressSanitizer: 256 byte(s) leaked in 1 allocation(s)
```

**Valgrind 输出关键词**：
```
==12345== LEAK SUMMARY:
==12345==    definitely lost: 256 bytes in 1 blocks
==12345==    indirectly lost: 0 bytes in 0 blocks
==12345==    possibly lost: 0 bytes in 0 blocks
==12345==    still reachable: 0 bytes in 0 blocks
```

**泄漏分类**：
| 类型 | 含义 |
|------|------|
| definitely lost | 没有任何指针指向这块内存 → 确定泄漏 |
| indirectly lost | 只有其他泄漏块里的指针指向它 → 间接泄漏 |
| possibly lost | 指针指向块内部而非开头 → 可能泄漏 |
| still reachable | 程序结束时还有指针指向 → 不是严重问题 |

**提前 return 泄漏**：Valgrind 会报同一个 `definitely lost`，但调用栈会显示错误路径的那一行。

---

## 4. 未初始化读取（Uninitialized Read）

**代码特征**：声明变量不赋值就读取 / 分支判断

**ASan**：通常**检测不到**。

**Valgrind 输出关键词**（需 `--track-origins=yes`）：
```
Conditional jump or move depends on uninitialised value(s)
   at ... main (xxx.c:7)
 Uninitialised value was created by a stack allocation
   at ... main (xxx.c:5)
```

**关键**：`--track-origins=yes` 让 Valgrind 追踪未初始化值的来源，否则只报"有未初始化值"不说从哪来。

---

## 5. 双重释放（Double Free）

**代码特征**：对同一指针 free 两次

**ASan 输出关键词**：
```
ERROR: AddressSanitizer: attempting double-free
    #0 ... in free
    #1 ... in main xxx.c:8           ← 第二次 free 的行
```

**Valgrind 输出关键词**：
```
Invalid free() / delete / delete[] / realloc()
   at ... free
   by ... main (xxx.c:8)
 Address 0x... is 0 bytes inside a block of size 32 free'd
```

**glibc（不用任何工具）**：较新版本会直接 abort 并打印：
```
free(): double free detected in tcache 2
Aborted (core dumped)
```

---

## 速查矩阵

| Bug 类型 | ASan 检测？ | Valgrind 检测？ | UBSan 检测？ |
|----------|------------|----------------|-------------|
| 堆溢出 | ✅ heap-buffer-overflow | ✅ Invalid write | ❌ |
| 栈溢出 | ✅ stack-buffer-overflow | ❌ | ❌ |
| Use-After-Free | ✅ heap-use-after-free | ✅ Invalid read/write | ❌ |
| 内存泄漏 | ✅ SUMMARY 汇总 | ✅ definitely lost | ❌ |
| 未初始化读取 | ❌ | ✅ Conditional jump | ❌ |
| 双重释放 | ✅ double-free | ✅ Invalid free | ❌ |
| 整数溢出 | ❌ | ❌ | ✅ signed integer overflow |

---

## 实战选工具决策树

```
开发中每次编译 → 加 -fsanitize=address（快，~2x 慢）
怀疑整数溢出 → 加 -fsanitize=undefined
提交前 / 怀疑泄漏 → valgrind --leak-check=full --track-origins=yes
栈溢出排查 → ASan（Valgrind 检测不到）
未初始化变量 → Valgrind（ASan 检测不到）
```
