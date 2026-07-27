# 任务二：间歇性 Bug 实验笔记

> 日期：2026-07-24  
> 实验：一字节堆缓冲区溢出 + 100次循环测试

---

## 实验设计

**程序**：`intermittent-bug.c`
- `malloc(8)` 分配 8 字节缓冲区
- 故意写 9 个字节（`buf[8] = random_byte`）——一字节越界
- 溢出位置在堆上，紧挨分配的 8 字节区域之后

**测试方法**：循环 100 次，每次运行独立进程，统计崩溃次数

---

## 结果对比

| 版本 | 崩溃 | 存活 | 检测率 |
|------|------|------|--------|
| 普通编译（无检测） | **0** | **100** | **0%** |
| ASan 编译 | **100** | **0** | **100%** |

---

## ASan 报告关键信息

```
ERROR: AddressSanitizer: heap-buffer-overflow
WRITE of size 1 at 0x... 
0 bytes after 8-byte region [0x...,0x...)
allocated by: main ... intermittent-bug.c:33  (malloc)
```

解读：
- `0 bytes after 8-byte region` → 溢出恰好发生在缓冲区边界之后
- `WRITE of size 1` → 写入操作，1 个字节
- ASan 直接指了 **分配行** 和 **越界写行**，不需要猜测

---

## 为什么普通版本 100% 存活？

### glibc tcache 的"宽容"

现代 glibc 使用 **tcache**（thread-local cache）管理小块内存：

```
free(p) → 放入 tcache 链表 → 不检查 chunk 元数据完整性
```

流程：
1. `buf[8]` 越界写入 → 写到相邻 chunk 的内存区域
2. `free(guard_block)` → tcache 直接回收，**不检查 chunk 一致性**
3. `free(buf)` → 同上

堆被破坏了，但没有人发现。就像一个房子里墙面裂了，但你从来没走进那个房间。

### 什么时候会崩？

- tcache bin 满了（默认 7 个）→ 转入 regular bin → 检查 prev_inuse 等标志
- `malloc` 时触发 consolidation（相邻 free chunk 合并）
- 溢出的那个字节碰巧破坏了其他重要数据
- 换一个 glibc 版本、换一台机器、换一个内存负载 → 可能立刻就崩

---

## 为什么 ASan 100% 检测？

### ASan 的 Redzone 机制

```
普通 malloc(8):
  [chunk header] [8 bytes usable] [next chunk...]

ASan malloc(8):
  [redzone] [8 bytes usable] [redzone] [next allocation's redzone] ...
```

每个分配区域前后都有 **redzone**（毒化区），任何对 redzone 的读写都被 ASan 立即拦截。

ASan 不需要等到 `free()`——**越界写入的瞬间**就触发 `ABORTING`。

---

## 核心教训

### 1. "没崩" ≠ "没 Bug"

```
测试环境跑 100 次 OK
       ↓
部署到生产环境
       ↓
换了 glibc 版本 / 堆布局不同 / 负载更高
       ↓
凌晨 3 点 segfault
```

这就是间歇性 Bug 最可怕的地方。

### 2. ASan 是第一道防线

| 场景 | 不用 ASan | 用 ASan |
|------|----------|---------|
| 开发时 | bug 悄悄存在 | 第一时间发现 |
| 测试时 | 碰运气 | 100% 拦截 |
| CI 管道 | 不可靠 | 每次必检 |

### 3. 堆溢出比栈溢出更隐蔽

- **栈溢出**：可能破坏返回地址、stack canary，相对容易被检测
- **堆溢出**：破坏堆元数据或相邻数据，tcache 的宽松检查让 bug 隐身更久

---

## 验证命令备忘

```bash
# 编译
gcc -Wall -g -O0 -o intermittent-bug intermittent-bug.c
gcc -fsanitize=address -g -O0 -o intermittent-bug-asan intermittent-bug.c

# 手动跑几次感受
./intermittent-bug
./intermittent-bug-asan

# 循环 100 次统计
chmod +x run-intermittent.sh
./run-intermittent.sh              # 普通版本
./run-intermittent.sh --asan       # ASan 版本
```
