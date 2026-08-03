# W5 教程：从 Echo Server 到 TCP 代理

> **你的位置**：Phase 1 — 系统基础与 TCP 网关  
> **本周目标**：用 epoll 实现第一个真正有用的网络中间件——TCP 代理  
> **前置**：W3（socket/connect）、W4（epoll/状态机/非阻塞 I/O）  
> **新增系统调用**：0 个——全部是 W3+W4 已学过的

---

## 知识地图

```
W4 echo server                  W5 TCP 代理
     │                               │
     client_fd                       client_fd ←──→ backend_fd
        │                               │              │
   read → write                       read → write   read → write
   (同一个 fd)                        (跨 fd！)       (跨 fd！)
```

**W5 不是学新 API，是学一种新的组合方式**——把 W3 的 `connect` 和 W4 的 `epoll` 拼在一起，造出一个能用的网络中间件。

---

## 第一阶段：为什么需要代理？

### 1.1 从 Echo 到转发

你 W4 写的 echo server 做的事：

```
客户端说 "hello" → server 回 "hello"
```

TCP 代理做的事：

```
客户端说 "hello" → 代理收到 → 原封不动发给后端
后端回 "world"   → 代理收到 → 原封不动发给客户端
```

代理**不关心数据内容**，它只是个搬运工。但这个搬运工是所有网络基础设施的原子操作：

| 你听过的东西 | 核心就是 TCP 代理 |
|-------------|------------------|
| 负载均衡器 | 代理 + 选择后端 |
| API 网关 | 代理 + 检查 HTTP 头 |
| TLS 终结器 | 代理 + 加解密 |
| VPN 隧道 | 代理 + 加密链路 |

### 1.2 本周你造的东西

```
                     你的 TCP 代理
   nc ──────────────► 0.0.0.0:9000 ──────────────► 127.0.0.1:8080 (echo server)
   客户端                                   后端
```

客户端不知道自己连的是代理，后端不知道自己前面有代理。代理对两端都是透明的。

---

## 第二阶段：三个新概念

### 概念一：非阻塞 connect

**为什么需要？**

W3 你写 `echod-client.c` 时，`connect()` 是阻塞的——直到后端 accept 了才返回。这在一个客户端连接一个服务器的场景下没问题。但在代理里：

```
客户端连上来 → 代理去 connect 后端 → 等了 3 秒 → 客户端早就超时了
                                        ↑
                                  这三秒内，其他所有客户端都被卡住
```

你不能让一个 `connect` 卡住整个事件循环。

**怎么办？**

和 W4 `listen_fd` 设非阻塞完全一样的思路——`connect` 也设成非阻塞：

```c
int bfd = socket(AF_INET, SOCK_STREAM, 0);
set_nonblocking(bfd);                         // ① 设非阻塞

int ret = connect(bfd, (struct sockaddr*)&backend_addr, sizeof(backend_addr));
if (ret < 0 && errno == EINPROGRESS) {
    // ② "正在连接中"——这不是错误，是正常的！
    // 把 bfd 注册到 epoll，等 EPOLLOUT 事件
    ev.events = EPOLLOUT;                     // ③ 注意：这里是 EPOLLOUT，不是 EPOLLIN
    ev.data.ptr = proxy_conn;
    epoll_ctl(epfd, EPOLL_CTL_ADD, bfd, &ev);
}
```

**为什么是 `EPOLLOUT` 而不是 `EPOLLIN`？**

`EPOLLIN` = "可读"——有数据到了  
`EPOLLOUT` = "可写"——连接建立好了，可以写了

非阻塞 connect 完成时，socket 变成"可写"状态。所以用 `EPOLLOUT` 来判断连接是否建立好了。

```
阻塞版：    connect() ───────────────────── 3 秒后返回 OK
非阻塞版：  connect() → EINPROGRESS → 注册到 epoll(EPOLLOUT)
                                          → 继续服务别人
                                          → epoll_wait 通知 EPOLLOUT → 连好了！
```

### 概念二：双向转发

Echo server 的读写在一个 fd 上：

```
read(fd) → write(fd)    // 同一个 fd，数据回流
```

代理的读写跨越两个 fd：

```
read(client_fd)  → write(backend_fd)    // 客户端的话，转给后端
read(backend_fd) → write(client_fd)     // 后端的回复，转给客户端
```

在事件循环里体现为三个分支：

```
epoll_wait
    │
    ├── client_fd 可读  →  read(client)  →  write(backend)
    │
    ├── backend_fd 可读 →  read(backend) →  write(client)
    │
    └── 任一 fd 断开    →  close(另一端) + 清理
```

**方向箭头图**：

```
   client                    agent                   backend
      │                        │                        │
      │ ──── "hello" ────────► │                        │
      │                        │ ──── "hello" ────────► │
      │                        │                        │
      │                        │ ◄──── "world" ──────── │
      │ ◄──── "world" ──────── │                        │
```

### 概念三：连接对

W4 状态机的 `conn_t` 管一个连接。代理需要管**一对连接**：

```c
struct proxy_conn {
    int    client_fd;       // 客户端这边的 fd
    int    backend_fd;      // 后端那边的 fd

    // 两个方向的缓冲区
    // c2b = client to backend（客户端→后端）
    char   c2b_buf[4096];
    size_t c2b_len;

    // b2c = backend to client（后端→客户端）
    char   b2c_buf[4096];
    size_t b2c_len;
};
```

**这个结构体的本质**：把两个 W4 的 `conn_t` 背靠背拼在一起，中间不存数据内容，只是搬运通道。

对比：

```
W4 conn_t:   一个 fd + 它的状态
W5 proxy_conn: 两个 fd + 它们之间的数据通道
```

---

## 第三阶段：分批写代码

### Step 1：让后端先跑起来（1.1 复习）

代理需要转发到后端，所以我们先确保有一个后端在跑。用你 W4 的 `epoll-echo.c`：

```bash
# 终端1：启动 echo server 作为后端
./epoll-echo    # 监听 8080
```

### Step 2：搭骨架——监听 + epoll（已知内容）

和 W4 完全一样的开头：`socket → setsockopt → bind → listen → set_nonblocking → epoll_create1 → epoll_ctl(ADD, listen_fd) → 事件循环`。

区别只在一个配置常量：

```c
#define AGENT_PORT   9000     // 代理对外监听 9000
#define BACKEND_PORT 8080     // 后端在 8080
```

### Step 3：accept 客户端 + 发起后端连接（新内容）

这是本周最关键的代码段。客户来了不是 echo，而是**去连后端**：

```
accept(client_fd)                                          // W3 已知
    → socket() + set_nonblocking(backend_fd)               // W3 已知
    → connect(backend_fd, ...) → 期望返回 EINPROGRESS      // W5 新：非阻塞 connect
    → 创建 proxy_conn，绑好 client_fd + backend_fd         // W5 新：连接对
    → epoll_ctl(ADD, client_fd, EPOLLIN)                   // W4 已知：盯客户端
    → epoll_ctl(ADD, backend_fd, EPOLLOUT)                 // W5 新：等连接完成
```

**为什么 backend_fd 注册 `EPOLLOUT` 而不是 `EPOLLIN`？** 回到概念一——非阻塞 connect 完成时 fd 变成"可写"，用 `EPOLLOUT` 来捕捉"连接好了"这个事件。等连接确认后，再 `EPOLL_CTL_MOD` 改成 `EPOLLIN`（等后端回数据）。

### Step 4：事件循环——三个分支（核心逻辑）

```c
for (int i = 0; i < nfds; i++) {
    拿到就绪的 fd → 通过 data.ptr 找到它属于哪个 proxy_conn

    判断是谁就绪：

    if (fd == listen_fd) {
        // 分支一：新客户端来了
        // → accept → 创建 backend socket → 非阻塞 connect → 创建 proxy_conn
    }
    else if (fd == proxy_conn->client_fd) {
        // 分支二：客户端发了数据
        // → read(client) → 数据放进 c2b_buf → write(backend)
    }
    else if (fd == proxy_conn->backend_fd) {
        // 分支三：后端有回复
        // → read(backend) → 数据放进 b2c_buf → write(client)
    }

    // 任何 read 返回 0（对方断开）→ 关闭另一端 → 清理 proxy_conn
}
```

**这一步只用到了 W3 的 `connect` 和 W4 的 `epoll` + `data.ptr`**，没有任何新系统调用。

### Step 5：断开处理

一个连接断了，必须关掉对面：

```
client 断开 → close(backend_fd) → 清理 proxy_conn
backend 断开 → close(client_fd) → 清理 proxy_conn
```

**不做这一步的后果**：客户端跑了，后端还开着，慢慢积累成 fd 泄漏。

---

## 第四阶段：Demo 代码结构

完整 demo 放在 `week5/demo/tcp-agent.c`。你来写，但骨架是这样的：

```
main():
    server_init()           // socket/bind/listen（W3）
    epoll_create1()         // 创建 epoll（W4）
    epoll_ctl(ADD, listen)  // 注册接线员（W4）

    while (1):
        epoll_wait()        // 等人（W4）

        for each 就绪 fd:
            if fd == listen:
                client = accept()           // W3
                backend = socket() + connect()  // W3 + 新：非阻塞
                创建 proxy_conn                  // 新：连接对
                epoll_ctl(ADD, client)       // W4
                epoll_ctl(ADD, backend)      // W4

            elif fd == client:
                n = read(client, buf)        // W3
                if n <= 0: 关对面 + 清理
                else: write(backend, buf)    // 转发！

            elif fd == backend:
                n = read(backend, buf)       // W3
                if n <= 0: 关对面 + 清理
                else: write(client, buf)     // 转发！
```

---

## 核心概念速查

| 概念 | 一句话 |
|------|--------|
| TCP 代理 | 客户端←→代理←→后端，双向透明转发 |
| 非阻塞 connect | `connect()` 返回 `EINPROGRESS`，用 `EPOLLOUT` 等连接完成 |
| `EINPROGRESS` | "正在连接中"，不是错误——非阻塞 connect 的正常返回值 |
| `EPOLLOUT` 等连接 | 非阻塞 connect 完成时 fd 变成"可写"，所以用 EPOLLOUT |
| 双向转发 | read(A) → write(B)，read(B) → write(A) |
| 连接对 | 一个 client_fd + 一个 backend_fd + 双向缓冲区 = proxy_conn |
| 断开连锁 | 一端断开 → 必须关闭另一端 → 释放 proxy_conn |

---

## 和 W4 的对照

| | W4 echo server | W5 TCP 代理 |
|------|---------------|------------|
| 连接数 | 1 个 fd 管自己 | 2 个 fd 成对管 |
| read 后 | write 回同一个 fd | write 到另一个 fd |
| 状态结构体 | conn_t（一个 fd） | proxy_conn（两个 fd） |
| connect | 不需要（只做 server） | 需要——非阻塞连后端 |
| 新系统调用 | epoll_create1/ctl/wait | **0 个**——全是已知 API 的新组合 |

---

## W5 任务

### 任务一：非阻塞 connect 实验（~1h）

写一个最小 demo：创建 socket → 设非阻塞 → `connect()` 到 `127.0.0.1:8080`（先确保 echo server 在跑）→ 观察 `errno == EINPROGRESS` → 注册到 epoll → 等 `EPOLLOUT` 确认连接完成 → `write` 一条测试消息 → `read` 回显。

用 `strace` 对比：注释掉 `set_nonblocking` 看阻塞版的调用序列，再加上看非阻塞版的序列。

### 任务二：TCP 代理（~3h）

写 `tcp-agent.c`：
1. 监听 `0.0.0.0:9000`
2. 客户端连上来 → 代理连接 `127.0.0.1:8080`
3. 双向转发
4. 任一端断开 → 清理对面

验证：
```bash
# 终端1：先启动后端
./epoll-echo                    # W4 的，监听 8080

# 终端2：启动代理
./tcp-agent                     # 监听 9000，转发到 8080

# 终端3：客户端连代理
nc localhost 9000
> hello                         # 应该收到 echo "hello"——经过代理转发的！
```

### 任务三：笔记 + 架构图（~1h）

用你自己的话画一张图，标注：
- 代理有几个 epoll？
- epoll 里注册了几个 fd？
- 数据从客户端到后端走了哪条路径？
- 哪个 fd 用了 `EPOLLOUT`？为什么是它？

---

好了，开始任务一。先确保你的 `epoll-echo` 在 8080 跑起来，然后写最小 demo 验证非阻塞 connect。遇到问题按三步排错流程来。
