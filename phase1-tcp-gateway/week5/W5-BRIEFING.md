# W5 课前简报：从 Echo Server 到 TCP 代理

> **日期**：2026-08-03  
> **阶段**：Phase 1 — 系统基础与 TCP 网关  
> **衔接**：W4 你用 epoll 写出了并发 echo server → W5 把它升级为**第一个能用的网络代理**

---

## 一、为什么学这个？

你现在写的东西是这样的：

```
客户端 → echo server（原样返回）
```

这没什么实际用处。真正的网络中间件是这样的：

```
客户端 ──→ 代理网关 ──→ 后端服务器
             │
             └── 客户端说的话，原封不动转给后端
                 后端回复的话，原封不动转给客户端
```

**所有网络基础设施都长这样**：负载均衡器、API 网关、TLS 终结器、VPN 隧道、正向/反向代理。你 Phase 1 的目标项目 "Mini TCP Gateway" 的核心就是这个模式。

**W5 是 Phase 1 的转折点**——前面四周全是为了让你有能力写出本周的代码。W5 写完，你就有了一个真正能用的东西。

---

## 二、知识地图

```
W4 的能力：epoll 同时盯多个 fd，谁有动静处理谁
              │
              ▼
W5 要加的东西：不只要"盯客户端"，还要"盯后端"

              ┌─────────────────────────────┐
              │   一个 client 连上来         │
              │        │                    │
              │        ▼                    │
              │   代理主动连接后端（connect）  │  ← 新概念①：非阻塞 connect
              │        │                    │
              │        ▼                    │
              │   两条通道同时运转：           │
              │   client → agent → backend   │  ← 新概念②：双向转发
              │   client ← agent ← backend   │
              │        │                    │
              │        ▼                    │
              │   任一端断开 → 关掉另一端     │  ← 新概念③：连接对管理
              └─────────────────────────────┘
```

**本周三个新概念，每个都很简单：**

| 概念 | 一句话 | 复杂度 |
|------|--------|--------|
| 非阻塞 connect | 去连后端时不能卡住——用 epoll 盯 connect 是否完成 | ★★☆ |
| 双向转发 | 从 A 读到的数据，写到 B；从 B 读到的数据，写到 A | ★★☆ |
| 连接对 | 用结构体把 client_fd 和 backend_fd 绑成一对 | ★☆☆ |

---

## 三、核心概念预览

### 概念一：非阻塞 connect

W3 你写的 `echod-client.c` 里 `connect()` 是阻塞的——直到后端 accept 了才返回。但在代理里，你不能等——你同时在服务几十个客户端，等后端 3 秒钟就是 3 秒的全面死机。

**解决**：把连接后端的 socket 也设成非阻塞，`connect()` 立刻返回 `EINPROGRESS`（"正在连接中，别等"），然后把 fd 注册到 epoll，等 `EPOLLOUT` 事件告诉你"连接建立好了"。

```
阻塞 connect：    connect() ———————————————————— 3 秒后返回 ✓
非阻塞 connect：  connect() → EINPROGRESS → 注册到 epoll → 继续服务别人
                                                          → epoll_wait 通知你"连好了"
```

### 概念二：双向转发

你现在 echo server 的逻辑是"读 → 写回同一个 fd"。代理的逻辑是"读 A → 写到 B"：

```
                     epoll_wait
                         │
          ┌──────────────┼──────────────┐
          ▼              ▼              ▼
     client_fd 可读   backend_fd 可读   任一 fd 断开
          │              │              │
          ▼              ▼              ▼
    read(client)     read(backend)   关闭另一端
          │              │
          ▼              ▼
   write(backend)   write(client)
```

**关键认知**：转发的代码比 echo 还简单——你不需要理解数据内容，只是搬运工。

### 概念三：连接对

```
struct proxy_conn {
    int client_fd;       // 客户端这边
    int backend_fd;      // 后端那边
    int backend_status;  // CONNECTING / CONNECTED / CLOSED

    // 两个方向的缓冲区（因为非阻塞 write 可能只写一半）
    char   c2b_buf[BUF_SIZE];   // client → backend
    size_t c2b_len;
    char   b2c_buf[BUF_SIZE];   // backend → client
    size_t b2c_len;
};
```

**和 W4 状态机的区别**：W4 的 `conn_t` 是一个连接的"记忆"，W5 的 `proxy_conn` 是**一对连接的"记忆"**——你能随时知道"这个客户对应的后端是谁"。

---

## 四、实验任务（新教学模式：审查 + 验证）

**核心变化**：不让你手写代码。代码我已经写好（`demo/tcp-agent.c`），你的任务是**读懂它、审查它、验证它**。

| 任务 | 内容 | 预估 |
|------|------|------|
| 任务一：代码审查 | 打开 `W5-REVIEW.md`，回答设计决策 + 正确性问题（口述即可） | ~2h |
| 任务二：strace 验证 | 跑起来，用 strace 观察完整的调用链路，理解 connect/read/write 的序列 | ~1h |
| 任务三：画数据流向图 | 用 ASCII 画出"客户端→代理→后端→代理→客户端"的完整数据流 | ~0.5h |

---

## 五、验收标准

| 标准 | 怎么验证 |
|------|---------|
| 理解非阻塞 connect | 审查 D1 题：`backend_ready` 字段是 bug 还是冗余？ |
| 理解双向转发 | 审查 C1 题：`forward` 函数有两个简化假设，是哪两个？ |
| 理解连接对管理 | 审查 C2 题：`cleanup_conn` 的顺序有什么问题？ |
| 画图 | 画出完整数据流向图（A1 题） |
| 实验验证 | strace 观察 connect 的返回值是 EINPROGRESS 还是 0，解释为什么 |

---

## 六、预估时间

| 内容 | 时间 |
|------|------|
| 教程阅读 + 概念理解 | 2h |
| 任务一：非阻塞 connect 实验 | 1h |
| 任务二：TCP 代理编码 | 3h |
| 任务三：多后端代理 | 2h |
| strace 验证 + 笔记 | 1h |
| **合计** | **~9h** |

---

## 七、和 W4 的关系

W5 不需要任何全新的系统调用——你只需要 `socket`, `bind`, `listen`, `accept`, `connect`, `read`, `write`, `fcntl`, `epoll_create1`, `epoll_ctl`, `epoll_wait`。**全是 W3 和 W4 的东西。** 新的是**把这些系统调用组合成一个新模式**——从"echo"到"转发"。

这就是框架学习——不是学新 API，是学新**组合方式**。

---

**准备好了就说"开始 W5"，我交付完整教程。**
