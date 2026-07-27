# W2 验收考察 — 问题与答案

> 日期：2026-07-27  
> 得分：估分 ~60/100（部分概念需强化）

---

## 第 1 题：间歇性堆溢出（20 分）

**原题**：同事写了 `malloc(8)` + `strcpy(buf, input)`，开发机 50 次没崩，服务器 2 小时后崩了。

**答案**：

### 1. Bug 类型
**堆缓冲区溢出**。`malloc(8)` 只分配 8 字节，`strcpy` 不检查长度，input 超过 8 字节就写穿 `buf`。

### 2. 溢出覆盖了什么
堆的实际布局不是你以为的两块并排：
```
[chunk header: 16B] [buf data: 8B] [chunk header: 16B] [meta data: 16B]
                                  ↑ 越界 → [meta 的 prev_size 和 size 被破坏]
```
溢出的字节覆盖了 `meta` 的 **chunk header**（prev_size + size 字段），而不是 meta 的用户数据。

### 3. tcache 为什么不检查
**tcache**（Thread-Local Cache）是 glibc 2.26 引入的每线程快速缓冲区。每个 bin 最多存 7 个同大小的 chunk。

tcache 的 free 伪代码：
```c
tcache_put(chunk) {
    chunk->next = tcache_bin->head;
    tcache_bin->head = chunk;
    // 不检查 prev_size，不检查 size，不验证相邻 chunk
}
```

**bin** = 按大小分类的通用回收箱（fast bin / small bin / large bin / unsorted bin）。

### 4. 为什么几小时后才崩
- tcache bin 最多存 **7 个**同大小 chunk
- 前 7 次 free → 进 tcache → 不检查 → 不崩
- 第 8 次 free → tcache 满了 → 转入 regular bin → 检查 prev_inuse 标志 → 发现 chunk header 被破坏 → **abort()**

开发机每次测试都是独立进程，tcache 从零开始走不满。服务器持续运行，tcache 逐渐被填满。

### 5. 用什么工具
**ASan**：编译时在每个 malloc 块周围加 redzone，越界写入瞬间检测。
**Valgrind**：模拟执行，也能检测，但不需要重新编译。

### 关键概念
| 概念 | 一句话 |
|------|--------|
| chunk | malloc 给的每块堆内存前面都有元数据 |
| chunk header | 存 prev_size + size |
| bin | 按大小分类的回收箱 |
| tcache | 每线程快速通道，不检查，最多 7 个 |
| 间歇性 | tcache 空时不查，满了转入 bin 才查 |

**薄弱点**：学生对 tcache/bin 的内部机制不熟悉（自述"不懂"）。

---

## 第 2 题：ASan vs Valgrind 原理（15 分）

**原题**：比较 ASan 和 Valgrind 的工作原理。

**答案**：

### 1. ASan 检测堆溢出
编译时插桩。每个 `malloc(N)` 实际分配：
```
[左 redzone: 不可访问] [N 字节用户可用] [右 redzone: 不可访问]
```
任何对 redzone 的读/写，ASan 在**越界发生的瞬间**就拦截并 abort。

### 2. Valgrind 检测未初始化内存
Valgrind 不重新编译，而是**模拟执行**每条指令。它维护一套 **V-bits（影子位）**，追踪每个比特是否被初始化过：
- V-bit = 1 → 这个 bit 被写过
- V-bit = 0 → 这个 bit 从未被写

读一个未被初始化的 bit → Valgrind 报 `Use of uninitialised value`。

### 3. ASan 检不了、Valgrind 能检的 Bug
**未初始化内存读取**。ASan 只做边界检查（有没有写到不该写的地方），不关心读的值是什么。Valgrind 的 V-bits 专门负责这件事。

### 工具分工
| | ASan | Valgrind |
|---|---|---|
| 检测方式 | 编译时插入检查代码 | 运行时模拟执行 |
| 速度 | 慢 ~2x | 慢 10-50x |
| 堆/栈溢出 | ✅ | ✅ |
| UAF / Double Free | ✅ | ✅ |
| 未初始化读 | ❌ | ✅⭐ |
| 需要重新编译 | 是 | 否 |

**薄弱点**：学生对 Valgrind 的 V-bits 原理不了解。

---

## 第 3 题：错误路径内存泄漏（10 分）

**原题**：`fopen` 失败时 `return NULL`，但前面已经 `malloc(256)` 的 `data` 没有释放。

**答案**：

**Bug**：内存泄漏（提前返回路径）。`fopen` 失败时 `return NULL` 之前的 `data` 泄漏了。

**Valgrind 报**：`definitely lost` — 指向 `data` 的指针丢失，256 字节无法再释放。

**修复（goto cleanup 模式）**：
```c
char* read_config(const char* path) {
    FILE* f = NULL;
    char* data = NULL;

    data = malloc(256);
    if (!data) goto cleanup;

    f = fopen(path, "r");
    if (!f) goto cleanup;

    fread(data, 1, 256, f);
    fclose(f);
    f = NULL;

cleanup:
    if (f) fclose(f);
    return data;   // 成功时返回 data，失败时 data 是 NULL
}
```

**薄弱点**：学生最初误判为"f 未初始化 / 野指针"，未注意到资源泄漏。

---

## 第 4 题：Off-by-One（10 分）

**原题**：`for (int i = 0; i <= n; i++)`，参数叫 `n` 而不是 `end`。

**答案**：

1. **Bug 类型**：**Off-by-One** 溢出。循环跑了 n+1 次，`arr[n]` 越界了一个元素。

2. **为什么难发现**：只溢出一个元素，可能落在对齐填充、栈 canary 之后、或者刚好是另一个变量的安全区域。溢出量太小，不破坏关键数据时完全无感知。

3. **实验数据**：任务二中，`buf[8]` 一字节堆溢出：
   - 普通版本：**0% 崩溃**（tcache free 不检查 chunk header）
   - ASan 版本：**100% 检测**（redzone 被写入，瞬间拦截）

**薄弱点**：学生最初认为"可能逻辑就是 0..n"，未理解 C 惯例中 `n` 代表元素个数。

---

## 第 5 题：泄漏分类与 goto cleanup（15 分）

**原题**：解释三种泄漏类型 + goto cleanup 模式。

**答案**：

### 1. 三种泄漏

| 类型 | 含义 | 比喻 |
|------|------|------|
| **definitely lost** | 指向这块内存的指针彻底没了 | 钥匙扔了 |
| **indirectly lost** | 上层结构被 leak，子块跟着丢 | 保险箱被扔了，里面的东西也没了 |
| **possibly lost** | 指针还在，但不指向内存起始位置 | 有钥匙但不知道哪个门 |
| **still reachable** | 指针完好，但程序结束前没主动 free | 钥匙在口袋，走时忘了还 |

**最重要**：definitely lost 和 indirectly lost 是真正的 Bug。still reachable 在短命程序中 OS 会回收，但服务器程序必须修复。

### 2. goto cleanup 模式

C 语言没有异常、析构函数。多个资源、多个错误路径时，goto cleanup 把所有释放集中一处。

```c
int process(const char* path) {
    FILE* f = NULL;
    char* buf = NULL;

    f = fopen(path, "r");
    if (!f) goto cleanup;

    buf = malloc(1024);
    if (!buf) goto cleanup;

    // 正常处理...

cleanup:
    if (f) fclose(f);
    free(buf);         // free(NULL) 是安全的
    return 0;
}
```

**三个原则**：
1. 所有指针初始化 NULL，fd 初始化 -1
2. 释放顺序与分配顺序相反（洋葱法则）
3. `free(NULL)` / `close(-1)` 是安全的，不需额外判断

---

## 总结

| 题号 | 估分 | 核心薄弱点 |
|------|------|-----------|
| 第 1 题 | 12/20 | tcache/bin 机制不熟 |
| 第 2 题 | 10/20 | Valgrind V-bits 原理不熟 |
| 第 3 题 | 8/20 | 未识别资源泄漏 |
| 第 4 题 | 10/20 | 未理解 off-by-one 命名惯例 |
| 第 5 题 | 10/20 | goto cleanup 理解了但没写代码 |
| **总计** | **~50/100** | 需要 W3 加强工具原理 + 资源管理模式 |
