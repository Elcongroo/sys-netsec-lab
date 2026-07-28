# W4：I/O 多路复用与 epoll

> **你的位置**：Phase 1 — 系统基础与 TCP 网关  
> **本周目标**：理解为什么需要 epoll、掌握 epoll 三个系统调用、写出并发 echo server  
> **衔接**：W3 你写出了阻塞 echo server → 发现单线程一个 `read` 卡住全部死 → W4 就是来解决这个的  

---

## 知识地图

```
W3 的痛：read(client_A) 阻塞 → 全部客户端卡住
              │
              ▼
W4 的解：epoll —— 一个线程同时监控所有 client fd
              │
              ▼
        epoll_create  →  创建 epoll 实例
        epoll_ctl     →  往 epoll 里"注册"你要紧盯的 fd
        epoll_wait    →  等，直到有任何一个 fd 就绪
              │
              ▼
        epoll echo server（真正的并发服务器原型）
```

---

## 第一阶段：从问题出发——为什么需要 epoll？

### 1.1 回顾 W3 的死局

你有 1000 个客户端连上来，但只有 1 个线程。你的代码是这样：

```c
while (1) {
    int client = accept(listen_fd, ...);   // 等新连接
    read(client, buf, 1024);               // 等这个客户发数据 ← 卡住了
    write(client, buf, n);
    close(client);
}
```

问题不是"不知道该怎么做"，而是**read 这个操作天然就是阻塞的**。你不等，数据没到；你等了，其他客户端被晾在一边。

### 1.2 理想的模型是什么？

你希望的模型：

```
"操作系统，帮我盯着这些 fd：
  - listen_fd：有人敲门（新连接）就叫醒我
  - client_fd_1：有数据到了就叫醒我
  - client_fd_2：有数据到了就叫醒我
  - ...
  任何一个有动静，你都告诉我。"

你不再主动"等"某个 fd——你**等操作系统通知**。
```

这就是 **I/O 多路复用**（I/O Multiplexing）的本质：**一个线程，同时监控多个 fd，谁先就绪先处理谁。**

### 1.3 类比：前台 vs 监控室

```
阻塞模型 = 一个前台接待员
  来一个客人 → 全程陪同 → 客人不走，接待员不下班
  其他客人在大厅等着

epoll 模型 = 一个监控员看 100 个屏幕
  哪个屏幕亮了（有事件）→ 处理那个 → 回去继续看屏幕
  不会因为屏幕 3 没动静就一直盯着屏幕 3
```

---

## 第二阶段：epoll 是什么？

### 2.1 一句话

**epoll 是 Linux 提供的 I/O 多路复用机制**——你告诉内核"帮我盯着这些 fd"，内核在任何一个 fd 就绪时通知你。

### 2.2 三个系统调用

| 系统调用 | 作用 | 调用频率 |
|---------|------|---------|
| `epoll_create1(0)` | 创建一个 epoll 实例，返回一个 fd | 一次 |
| `epoll_ctl(epfd, op, fd, event)` | 向 epoll 实例"注册/修改/删除"你要监控的 fd | 每个 fd 一次 |
| `epoll_wait(epfd, events, max, timeout)` | 等待事件发生，返回就绪的 fd 列表 | 循环调用 |

**关键认知**：`epoll_create1` 返回的是一个**特殊的 fd**。你对这个 fd 做操作，就像操作普通文件一样。

---

## 第三阶段：一步一步写出 epoll echo server

### 预备知识速查

这段代码里会出现的新符号：

| 符号 | 一句话解释 |
|------|-----------|
| `epoll_create1(0)` | 创建 epoll 实例，返回 fd |
| `struct epoll_event` | 描述"你要关注这个 fd 的哪些事件" |
| `EPOLLIN` | "可读"事件——有数据到了 / 有新连接了 |
| `EPOLL_CTL_ADD` | 操作类型：往 epoll 里添加一个 fd |
| `epoll_wait` | 等待事件，返回就绪 fd 的数量 |

### Step 1：创建 epoll 实例

```c
int epfd = epoll_create1(0);
//               │         │
//               │         └── flags=0 表示默认行为
//               └── 返回一个"epoll fd"
```

这就像在操作系统里**开了一个监控室**。你现在有了一个房间号（epfd），接下来要往房间里**放屏幕**（注册 fd）。

### Step 2：把 listen_fd 注册进去

```c
struct epoll_event ev;

ev.events = EPOLLIN;          // 事件类型："可读"——有连接来了就是可读
ev.data.fd = listen_fd;       // 把这个 fd 和事件绑定

epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);
//        │       │               │           │
//        │       │               │           └── 事件描述
//        │       │               └── 要监控的 fd
//        │       └── 操作：添加
//        └── epoll 实例
```

`epoll_ctl` 的意思是：**"在 epfd 这个监控室里，放上 listen_fd 这个屏幕，当它有 EPOLLIN 事件时就通知我。"**

### Step 3：等待事件——epoll_wait

```c
#define MAX_EVENTS 64

struct epoll_event events[MAX_EVENTS];  // 内核把就绪事件写到这里

while (1) {
    int nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);
    //           │       │        │        │          │
    //           │       │        │        │          └── timeout: -1 = 永远等
    //           │       │        │        └── 最多返回多少个事件
    //           │       │        └── 内核把就绪事件写到这个数组里
    //           │       └── epoll 实例
    //           └── 返回值 = 有几个 fd 就绪了
```

**这是整个 epoll 最核心的一行**。阻塞模型是"你主动等一个 fd"，epoll 是"内核告诉你哪些 fd 准备好了"。

```
阻塞模型:  read(client1) → 等 client1  → 等到了 → 返回
epoll:     epoll_wait()   → 等任何人   → client3 就绪！→ 处理 client3
```

### Step 4：处理就绪事件

```c
for (int i = 0; i < nfds; i++) {
    int fd = events[i].data.fd;  // 这就是就绪的那个 fd

    if (fd == listen_fd) {
        // listen_fd 就绪 → 有新连接来了！
        int client_fd = accept(listen_fd, ...);
        // 把 client_fd 也注册到 epoll 里，盯着它
        ev.events = EPOLLIN;
        ev.data.fd = client_fd;
        epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &ev);
    } else {
        // client_fd 就绪 → 有数据到了！
        char buf[1024];
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) {
            // 客户端断开了 → 从 epoll 里移除这个 fd
            close(fd);
        } else {
            write(fd, buf, n);  // echo 回去
        }
    }
}
```

**关键逻辑**：
- `fd == listen_fd` → 接线员在敲门 → `accept()` → 把新客户也加入监控
- `fd != listen_fd` → 某个客户有数据 → `read()` → `write()` echo 回去

### 完整的 epoll 循环图

```
epoll_wait()  ← 内核说"这几个 fd 有动静"
      │
      ▼
for each 就绪的 fd:
      │
      ├── fd == listen_fd?  →  accept()  →  新 client_fd 加入 epoll
      │
      └── fd != listen_fd?  →  read()  →  有数据?  →  write() echo
                              read()  →  返回 0?   →  close(), 从 epoll 移除
                              read()  →  出错?     →  close(), 从 epoll 移除
      │
      ▼
回到 epoll_wait()  ← 等待下一批事件
```

---

## 第四阶段：Level-Triggered vs Edge-Triggered

### 4.1 这是什么？

epoll 有两种工作模式。理解这两个概念是面试高频考点。

**Level-Triggered（LT，水平触发）—— 默认模式**

"这个 fd 上还有数据吗？有 → 通知你。你读了吗？没读完 → 下次继续通知你。"

类比：有人敲门，你不开门，他就一直敲。

**Edge-Triggered（ET，边缘触发）**

"这个 fd 的状态从'没数据'变成了'有数据' → 通知你一次。你读不读？不读完 → 没人再提醒你，数据留在缓冲区。"

类比：有人发了一条短信，你看不看是你的事，他不会重复发。

### 4.2 什么时候用哪种？

| | LT（默认） | ET |
|---|---|---|
| 编程难度 | 简单 | 复杂——必须循环读直到 EAGAIN |
| CPU 开销 | 略高（重复通知） | 更低（只通知一次） |
| 适用场景 | 所有场景都可以 | 高并发 + 非阻塞 fd |
| 漏事件风险 | 无 | 有——不读完就丢了 |

**初学者先用 LT**。Nginx 用 ET 追求极致性能。我们先掌握 LT，等你理解了再切 ET。

---

## 第五阶段：epoll vs select vs poll

面试常问："epoll 比 select 好在哪？"

| | select | poll | epoll |
|---|--------|------|-------|
| fd 数量上限 | 1024（FD_SETSIZE） | 无上限 | 无上限 |
| 每次调用 | 把全部 fd 列表**拷贝**给内核 | 同 select | 只注册一次，不重复拷贝 |
| 查找就绪 fd | O(n) 遍历全部 | O(n) 遍历全部 | O(1) —— epoll_wait 直接返回就绪列表 |
| 10000 个连接时 | 每次都要传 10000 个 fd → 慢 | 同 select | 只传就绪的那几个 → 快 |

**epoll 的核心优势**：**"只关注有动静的 fd"**。select/poll 像点名——每次把 10000 个人名念一遍，看谁举手。epoll 像监控——只告诉你谁动了。

---

## 核心概念速查

| 概念 | 一句话 |
|------|--------|
| I/O 多路复用 | 一个线程同时监控多个 fd，谁就绪谁先处理 |
| `epoll_create1(0)` | 创建 epoll 实例，返回一个 fd |
| `epoll_ctl` | 往 epoll 里注册/修改/删除 fd |
| `epoll_wait` | 等待事件，返回就绪 fd 的列表 |
| `EPOLLIN` | "可读"事件——有数据可读 / 有新连接 |
| LT（水平触发） | 只要 fd 上还有数据，就反复通知你 |
| ET（边缘触发） | 只在"没数据→有数据"的瞬间通知一次 |

---

## W4 任务

### 任务一：epoll echo server（编码 A，~3h）

1. 用 epoll 重写 echo server，支持**同时处理多个客户端**
2. 用 `nc localhost 8080` 开 3 个终端同时连接——验证三个都能正常 echo
3. 对比 epoll 版和 W3 阻塞版的核心差异（写一段文字总结）

### 任务二：用 strace 看 epoll 的工作过程（实验，~1h）

1. `strace -e trace=epoll_create,epoll_ctl,epoll_wait,accept,read,write -p <PID>` 跟踪你的 epoll server
2. 连接、发送数据、断开——观察 epoll_wait 的返回值变化
3. 记录：`epoll_wait` 返回了哪些就绪 fd？

### 任务三：改造为"有限状态机"模式（编码 B，~2h）

1. 给每个 client_fd 绑定一个状态结构体（存 receive buffer、已发送字节数等）
2. 让 echo server 不再是"读一行 echo 一行"——而是读满 1024 字节或遇到 `\n` 才 echo
3. 解释：为什么 epoll + 非阻塞模式下需要状态机？
