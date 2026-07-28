# W4 任务一～三：完整笔记

> 日期：2026-07-28  
> 文件：`my-lab/epoll-echo.c` / `my-lab/epoll-stateful.c` / `demo/epoll-server.c`

---

## 任务一：epoll echo server

### 核心对比：W3 阻塞 vs W4 epoll

```
W3 阻塞模型（blocking-server.c）:
  accept() → read(client_A) 阻塞 → write → close
                                     ↑
                              client_B 永远等不到 accept

W4 epoll 模型（epoll-echo.c）:
  epoll_wait() → 就绪列表中有 fd=[listen_fd, client_A, client_B, ...]
              → 遍历处理每个就绪 fd
              → 是 listen_fd 就绪? → accept
              → 是 client_fd 就绪? → read + echo
              → 回到 epoll_wait()
```

### 三个系统调用的关系

```
epoll_create1(0)  ──→  返回 epfd（一个可读可写的特殊 fd）
                           │
              ┌────────────┤
              ▼            ▼
      epoll_ctl()    epoll_wait()
      "添加/删除fd"   "等事件发生"

      调用一次/每fd    每次循环调用
```

### 实验验证

```bash
# 编译
gcc -Wall -g -O0 -o epoll-echo epoll-echo.c

# 启动 server
./epoll-echo

# 开 3 个终端
# 终端1: nc localhost 8080 → 输入 "hello from 1"
# 终端2: nc localhost 8080 → 输入 "hello from 2"
# 终端3: nc localhost 8080 → 输入 "hello from 3"
# 三个都能收到 echo！和 W3 的 blocking-server 完全不一样
```

---

## 任务二：strace 观察 epoll

### 实验

```bash
# 终端1：启动 server
./epoll-echo

# 终端2：找 PID 并 strace
ps aux | grep epoll-echo
strace -e trace=epoll_create1,epoll_ctl,epoll_wait,accept,read,write -p <PID> 2>&1

# 终端3：nc localhost 8080，输入 "hello"，按回车
```

### strace 典型输出（完整解读）

```
epoll_wait(5, [{EPOLLIN, {u32=3, u64=3}}], 64, 1000) = 1
    ↑         ↑                      ↑     ↑        ↑
    epfd=5    就绪事件数组             listen_fd=3  返回 1 个就绪 fd

accept(3, ...) = 6                      ← listen_fd 可读 = 有新连接，fd=6

epoll_ctl(5, EPOLL_CTL_ADD, 6, ...) = 0 ← 把 client_fd=6 加入 epoll

epoll_wait(5, [{EPOLLIN, {u32=6, u64=6}}], 64, 1000) = 1
    ↑                                      ↑
    epfd=5                                 client_fd=6 就绪

read(6, "hello\n", 4096) = 6              ← 读到 6 字节
write(6, "hello\n", 6) = 6                ← echo 回去 6 字节

epoll_wait(5, ..., 64, 1000) = 0           ← 超时（1s 内没事件）

epoll_wait(5, [{EPOLLIN, {u32=6, u64=6}}], 64, 1000) = 1
read(6, "", 4096) = 0                      ← EOF：客户端断开

epoll_ctl(5, EPOLL_CTL_DEL, 6, ...) = 0   ← 从 epoll 移除
```

### 关键观察

| epoll_wait 返回值 | 含义 |
|-------------------|------|
| > 0 | 有 N 个 fd 就绪 |
| 0 | 超时——这段时间内没有任何 fd 就绪 |
| -1 | 错误（或被信号打断） |

---

## 任务三：有限状态机 + epoll

### 为什么需要状态机？

假设客户端发了 1000 字节的数据。TCP 可能分 3 个段到达：

```
时间线：
  T1: epoll_wait 返回，client_fd 可读
      read() → 读了 400 字节 → 回到 epoll_wait
  T2: epoll_wait 返回，client_fd 可读
      read() → 又读了 350 字节 → 回到 epoll_wait
  T3: epoll_wait 返回，client_fd 可读
      read() → 读了 250 字节（满了）
```

**如果没有状态结构体，你怎么记得 T1 时读到了哪里？**

每一轮 `read` → `epoll_wait` → `read` 之间，线程没有"记忆"。
状态结构体（`conn_t`）就是你的**外部记忆**。

### 状态转换图

```
  STATE_READ ──→ 读到 \n 或 缓冲区满 ──→ STATE_WRITE
      ↑                                        │
      │                                        │
      └──── 写完所有数据 ──────────────────────┘
      
  任何时候 read=0 或 出错 ──→ STATE_CLOSE
```

### 非阻塞 write 也需要状态

非阻塞模式下 `write(fd, buf, n)` 可能只写了部分（比如 800/1000 字节）。
你需要记录 `send_done`——下次 `epoll_wait` 返回 `EPOLLOUT` 时从 `send_done` 继续写。

### 状态结构体（`conn_t`）

```c
typedef struct {
    int    fd;              // 客户端 fd
    conn_state_t state;     // STATE_READ / STATE_WRITE / STATE_CLOSE
    char   recv_buf[BUF_SIZE];  // 接收缓冲区
    size_t recv_len;        // 已接收字节数
    char   send_buf[BUF_SIZE];  // 发送缓冲区
    size_t send_len;        // 待发送总字节数
    size_t send_done;       // 已发送字节数
} conn_t;
```

### data.ptr vs data.fd

在 epoll-stateful 中，我们用了 `ev.data.ptr` 代替 `ev.data.fd`：

```c
ev.data.ptr = c;  // 把整个 conn_t 指针绑进事件里
```

这样 epoll_wait 返回时，直接从 `events[i].data.ptr` 拿到连接的所有状态，
不需要用 `fd` 去查表。

---

## 本周核心收获

1. **epoll 的三个系统调用**：`epoll_create1` → `epoll_ctl` → `epoll_wait`
2. **epoll_wait 是"等通知"**——你不是主动等某个 fd，而是等内核说"这几个 fd 有动静"
3. **状态机是非阻塞 I/O 的核心**——没状态，你不知道"上次读到了哪里"
4. **LT vs ET**：初学者用 LT（默认），Nginx 用 ET 追求极致性能
5. **epoll 比 select 好在哪**：只传就绪 fd——O(1) vs O(n)
