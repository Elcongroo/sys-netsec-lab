# broken-server.c — 8 个内存 Bug 修复笔记

> 日期：2026-07-24  
> 验证：Valgrind 3.26.0 — 0 errors, 0 leaks, 18 allocs / 18 frees

---

## Bug 总览

| # | 类型 | Valgrind 关键词 | 根因 | 修复方式 |
|---|------|----------------|------|---------|
| 1 | 未初始化内存 | Conditional jump depends on uninitialised value | malloc 后指针字段是垃圾值 | 显式初始化所有字段为 NULL/0 |
| 2 | 内存泄漏 | definitely lost | free 结构体前没释放内部堆指针 | 由内向外释放：先 free 成员，再 free 结构体 |
| 3 | 缓冲区溢出 | Invalid write / heap-buffer-overflow | 向 NULL 写数据 + 不用安全函数 | malloc + strncpy + 强制 '\0' |
| 4 | Use-After-Free | Invalid read ... inside ... free'd | free 后没置 NULL，后续代码读野指针 | free 后立即置 NULL |
| 5 | 未初始化内存读取 | Use of uninitialised value | malloc 后没填充数据就读取 | memset 清零 |
| 6 | 双重释放 | Invalid free() ... inside ... free'd | free 后没 return NULL，调用者又 free | free 后立即 return NULL |
| 7 | 错误路径泄漏 | definitely lost | 提前 return 时没释放已分配的资源 | goto cleanup 统一清理 |
| 8 | 空指针解引用 | SIGSEGV | 没检查 malloc 返回值 | malloc 后检查返回值 |

---

## 逐 Bug 详解

### BUG #1 — 未初始化内存

**原代码**：
```c
session_t* session_create(int id) {
    session_t* s = (session_t*)malloc(sizeof(session_t));
    s->id = id;
    s->state = STATE_NEW;
    /* recv_buf, packet_data, data_len 都没初始化！ */
    return s;
}
```

**问题**：`malloc` 不保证内存内容为零。`recv_buf` 和 `packet_data` 是指针，里面的值是随机的。后续代码 `if (s->recv_buf == NULL)` 读到了垃圾值，Valgrind 报 `Conditional jump depends on uninitialised value`。

**修复**：
```c
s->recv_buf    = NULL;
s->packet_data = NULL;
s->data_len    = 0;
```

**教训**：malloc 返回的内存内容是不确定的。每个字段都要显式初始化。或者用 `calloc`（自动清零，但性能略差）。

---

### BUG #2 — 释放结构体时泄漏内部指针

**原代码**：
```c
void session_destroy(int conn_id) {
    if (g_connections[conn_id] != NULL) {
        free(g_connections[conn_id]);  // 只释放了结构体本身
    }
}
```

**问题**：`session_t` 内部的 `recv_buf` 和 `packet_data` 各指向一块独立分配的堆内存。直接 `free(session_t)` 只释放了结构体那 40 字节，内部的 `recv_buf`（32 字节）和 `packet_data`（128 字节）变成了**孤儿内存**——没有指针指向它们，也无法再释放。

```
释放前：                       只 free 结构体后：
┌──────────┐                  ┌──────────┐ (已释放)
│ session_t│                  │ ???????? │
│ recv_buf─┼──→ [32 bytes]   │ ????????─┼──→ [32 bytes] ← 孤儿！
│ pkt_data─┼──→ [128 bytes]  │ ????????─┼──→ [128 bytes] ← 孤儿！
└──────────┘                  └──────────┘
```

**修复**：
```c
void session_destroy(int conn_id) {
    session_t* s = g_connections[conn_id];
    free(s->recv_buf);      // 先释放内部指针
    free(s->packet_data);
    free(s);                // 最后释放结构体
    g_connections[conn_id] = NULL;
}
```

**教训**：释放结构体前，先问自己——这个结构体里有没有指向堆内存的指针？有的话必须先释放。释放顺序是**由内向外**。

---

### BUG #3 — 缓冲区溢出（向 NULL 写数据）

**原代码**：
```c
int session_recv(session_t* s, const char* data) {
    if (s->recv_buf == NULL) {
        strncpy(s->recv_buf, data, BUF_SIZE-1);  // recv_buf 是 NULL！
    }
    return 0;
}
```

**问题**：第一层——`s->recv_buf` 是 NULL，往 NULL 写数据直接 SIGSEGV。第二层——即使用 `strcpy` 替代 `strncpy`，也无法防止超长输入覆盖相邻内存。

**修复**：
```c
int session_recv(session_t* s, const char* data) {
    if (s->recv_buf == NULL) {
        s->recv_buf = (char*)malloc(BUF_SIZE);
        if (s->recv_buf == NULL) return -1;
    }
    strncpy(s->recv_buf, data, BUF_SIZE - 1);
    s->recv_buf[BUF_SIZE - 1] = '\0';
    return 0;
}
```

**教训**：
- 往指针写数据前，确认指针指向了有效内存（已经 malloc）
- `strncpy` 的三个参数含义：目标、源、**最多复制多少字节**
- `strncpy` 有个坑：如果源字符串 >= 限制长度，它**不会**自动加 `\0`，所以必须手动加

---

### BUG #4 — Use-After-Free（野指针）

**原代码**：
```c
void session_close(int conn_id) {
    session_destroy(conn_id);       // free 了内存
    g_conn_count--;
    // g_connections[conn_id] 还指向已释放的内存！
}

// main 中：
session_close(0);
if (g_connections[0] != NULL) {     // 不是 NULL！通过检查
    printf("%d\n", g_connections[0]->state);  // 读野指针！
}
```

**问题**：`free` 释放了内存，但指针仍然保存着原来的地址。这块内存可能已经被其他 malloc 拿走了，你读到的值可能是任何东西。这就是 **Use-After-Free**，是最危险的内存 Bug 之一——它不一定每次都崩溃，但可能被安全漏洞利用。

**修复**：
```c
// 在 session_destroy 内部
g_connections[conn_id] = NULL;   // free 后立即置 NULL
```

**教训**：
- `free(p)` 之后，`p` 的值没有变，变的只是那块内存的"归属权"
- **free 后立刻置 NULL** 是最便宜的防御手段
- 如果有多个指针指向同一块内存，只置 NULL 一个是不够的（这就是为什么复杂项目用引用计数或智能指针）

---

### BUG #5 — 未初始化内存读取

**原代码**：
```c
char* session_get_data(session_t* s) {
    if (s->packet_data == NULL) {
        s->packet_data = (char*)malloc(128);
        /* 什么都没写进去！ */
    }
    return s->packet_data;
}

// main 中：
printf("数据: %02x %02x ...\n", data[0], data[1], ...);  // 读到垃圾值
```

**Valgrind 输出**：
```
Use of uninitialised value of size 8
Uninitialised value was created by a heap allocation
  at malloc ... session_get_data (broken-server.c:123)
```

**问题**：`malloc` 分配的内存内容是**不确定的**。打印这些字节时，Valgrind 追踪到了"这些值从未被写入过"。

**修复**：
```c
s->packet_data = (char*)malloc(128);
if (s->packet_data != NULL) {
    memset(s->packet_data, 0, 128);  // 清零，变成确定的全 0x00
}
```

**教训**：
- Valgrind 能追踪每一个字节的"初始化状态"——这是它最强大的能力之一
- `malloc` ≠ 零内存，`calloc` = 零内存
- 永远不要假设新分配的内存里是什么值

---

### BUG #6 — 双重释放（Double Free）

**原代码**：
```c
char* parse_header(const char* raw, size_t len) {
    char* header = (char*)malloc(HEADER_SIZE + 1);
    memcpy(header, raw, HEADER_SIZE);
    header[HEADER_SIZE] = '\0';

    if (header[0] == 'X') {
        free(header);
        // 没有 return NULL！继续往下走
    }
    return header;  // 返回已释放的指针
}

// process_packet 中：
header = parse_header(decoded, len);
// ... 使用 header ...
free(header);  // 第二次 free！→ Invalid free()
```

**时间线**：
```
1. parse_header 中 free(header)     ← 第一次释放
2. parse_header 返回 header          ← 调用者拿到已释放的指针
3. process_packet 中 free(header)    ← 第二次释放 → Valgrind 报错
```

**修复**：
```c
if (header[0] == 'X') {
    free(header);
    g_corrupted_packets++;
    return NULL;   // 关键！告诉调用者"没有 header"
}
```

**教训**：
- 函数内 `free` 了一个指针后，**必须**让调用者知道"这个指针已经无效了"
- 方法一：返回 NULL（适合返回值是指针的函数）
- 方法二：通过二级指针把调用者的指针也置 NULL（适合 void 函数）
- 方法三：统一在"拥有者"函数中释放，不让子函数释放它不拥有的内存

---

### BUG #7 — 错误路径上的内存泄漏

**原代码**：
```c
int process_packet(const char* raw_data, size_t len) {
    char* decoded = (char*)malloc(len);     // 分配 #1
    char* header  = NULL;
    char* payload = NULL;

    memcpy(decoded, raw_data, len);

    header = parse_header(decoded, len);
    if (header == NULL) {
        return -1;  // ← 泄漏：decoded 没释放！
    }

    payload = (char*)malloc(len - HEADER_SIZE);
    if (payload == NULL) {
        return -1;  // ← 泄漏：decoded 和 header 都没释放！
    }
    // ...
}
```

**问题**：C 语言没有异常处理、没有析构函数。每次 `return` 都必须手动清理之前分配的所有资源。错误处理路径越多，越容易漏。

**修复 — goto cleanup 模式**：
```c
int process_packet(const char* raw_data, size_t len) {
    char* decoded = NULL;
    char* header  = NULL;
    char* payload = NULL;

    decoded = (char*)malloc(len);
    if (decoded == NULL) return -1;
    memcpy(decoded, raw_data, len);

    header = parse_header(decoded, len);
    if (header == NULL) goto cleanup;

    if (len <= HEADER_SIZE) goto cleanup;

    payload = (char*)malloc(len - HEADER_SIZE);
    if (payload == NULL) goto cleanup;
    memcpy(payload, decoded + HEADER_SIZE, len - HEADER_SIZE);

    g_total_packets++;

cleanup:
    free(payload);   // free(NULL) 是安全的
    free(header);
    free(decoded);
    return 0;
}
```

**这个模式的精妙之处**：
- 所有指针初始化为 NULL
- 所有错误路径 `goto cleanup`
- cleanup 处按分配顺序的**反向**释放
- `free(NULL)` 是合法的——什么都没分配就跳过来也不会出错

**教训**：在 C 语言中 `goto` 不是恶魔。`goto cleanup` 是 Linux 内核源码中的标准模式，比重复写清理代码更清晰、更不易出错。

---

### BUG #8 — 空指针解引用

**原代码**：
```c
char* duplicate_string(const char* src) {
    size_t len = strlen(src);
    char* dst = (char*)malloc(len + 1);
    strcpy(dst, src);   // 如果 malloc 返回 NULL，这里 SIGSEGV
    return dst;
}
```

**问题**：内存不足时 `malloc` 返回 NULL，`strcpy(dst, src)` 向地址 0x0 写数据 → 段错误。

**修复**：
```c
char* dst = (char*)malloc(len + 1);
if (dst == NULL) {
    return NULL;
}
strcpy(dst, src);
```

**教训**：**永远**检查 `malloc` 的返回值。虽然 Linux 默认 overcommit 让 malloc 很少返回 NULL，但在嵌入式系统、严格限制内存的容器中，这是真实会发生的。

---

## 核心原则总结

1. **malloc 返回的内存不是零** —— 要么 `calloc`，要么 `memset`，要么逐字段初始化
2. **free 前先释放内部指针** —— 由内向外，先成员后结构体
3. **free 后立刻置 NULL** —— 让 Use-After-Free 尽早暴露
4. **错误路径必须清理已分配资源** —— goto cleanup 是最干净的方案
5. **永远检查 malloc 返回值** —— 哪怕"不可能"失败

---

## 验证命令备忘

```bash
# 编译
gcc -Wall -Wextra -g -O1 -o broken-server broken-server.c

# Valgrind 全面检查
valgrind --leak-check=full --track-origins=yes --show-leak-kinds=all ./broken-server

# ASan 快速检查
gcc -fsanitize=address -g -O1 -o broken-server-asan broken-server.c
./broken-server-asan
```
