# W3 任务一～三：完整笔记

> 日期：2026-07-27  
> 文件：`my-lab/echod-server.c` / `echod-client.c` / `blocking-server.c`

---

## 任务一：Echo Server + Client（部分读处理）

### 核心问题：为什么需要 "robust read/write"？

`read(fd, buf, 1024)` **不保证返回 1024**。它返回的是"当前可读的字节数"——可能只有 3、可能只有 1、可能恰好 1024。

**根因**：TCP 是字节流。对方写了 100 字节的数据，可能被拆成多个 TCP 段到达，每个段到达的时间不同。你的 `read` 只能拿"已经到达的"那部分。

**解决方案**：循环读 / 循环写。

```c
// 错误写法（假设一次读完）
char buf[1024];
read(fd, buf, 1024);  // 可能只读到了 3 字节！

// 正确写法（循环读到够）
ssize_t read_full(int fd, void* buf, size_t n) {
    size_t total = 0;
    while (total < n) {
        ssize_t nr = read(fd, (char*)buf + total, n - total);
        if (nr < 0 && errno == EINTR) continue;  // 被信号打断，重试
        if (nr < 0) return -1;                    // 真正的错误
        if (nr == 0) break;                       // EOF
        total += nr;
    }
    return (ssize_t)total;
}
```

### 三个返回值

| read 返回值 | 含义 | 应该做什么 |
|------------|------|-----------|
| > 0 | 读到了字节 | 继续处理 |
| 0 | **EOF**——对端关闭了连接 | 关闭 fd，结束 |
| -1, errno=EINTR | 被信号打断 | `continue` 重试 |
| -1, 其他 errno | 真正的错误 | 关闭 fd，log |

### 编译运行

```bash
# 编译
gcc -Wall -g -O0 -o echod-server echod-server.c
gcc -Wall -g -O0 -o echod-client echod-client.c

# 终端1：启动 server
./echod-server

# 终端2：用 nc 测试
nc localhost 8080

# 终端3：用自己写的 client 测试
./echod-client

# 用 strace 跟踪 server
ps aux | grep echod-server
strace -p <PID> -e trace=network
```

### strace 关键输出解读

当 `nc localhost 8080` 连接并发送 "hello" 时，strace 会显示：

```
socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) = 3   ← 创建 socket，返回 fd=3
bind(3, {sa_family=AF_INET, sin_port=htons(8080), sin_addr=inet_addr("0.0.0.0")}, 16) = 0
listen(3, 128)                                   = 0  ← 开始监听
accept(3, ...)                                   = 5  ← 有客户端来了，返回 client_fd=5
recvfrom(5, "hello\n", 4096, 0, ...)             = 6  ← 从 client_fd=5 读到 6 字节
sendto(5, "hello\n", 6, 0, ...)                  = 6  ← echo 回去 6 字节
close(5)                                                ← 关闭客户端
```

注意：strace 把 `read()` 显示为 `recvfrom()`，把 `write()` 显示为 `sendto()`——它们是等价的，只是 strace 的显示方式不同。

---

## 任务二：strace 实验

### 实验 1：strace cat /etc/hostname

只关注核心流程（忽略动态链接加载的部分）：

```
execve("/usr/bin/cat", ["cat", "/etc/hostname"], ...) = 0  ← 启动程序
openat(AT_FDCWD, "/etc/hostname", O_RDONLY) = 3            ← 打开文件，fd=3
read(3, "...", 65536)                           = 33        ← 读出 33 字节
write(1, "...", 33)                             = 33        ← 写到 stdout（fd=1）
read(3, "", 65536)                              = 0         ← EOF（文件读完了）
close(3)                                                    ← 关闭文件
exit_group(0)                                               ← 退出
```

**你认识的关键系统调用**：

| 系统调用 | 作用 | 我们用过吗 |
|---------|------|-----------|
| `execve` | 启动一个新程序 | — |
| `openat` | 打开文件（现代版的 `open`） | ✅ W1 demo |
| `read` | 从 fd 读数据 | ✅ W3 echo server |
| `write` | 向 fd 写数据 | ✅ W3 echo server |
| `close` | 关闭 fd | ✅ 一直用 |
| `exit_group` | 程序退出 | — |

### 实验 2：strace curl http://example.com

核心网络调用：

```
socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)              = 4   ← 创建 TCP socket
connect(4, {sin_port=htons(7897), sin_addr=127.0.0.1}) ... ← 连接代理
sendto(4, "GET http://example.com/ HTTP/1.1\r\n..."...) = 123 ← 发送 HTTP 请求
recvfrom(4, "HTTP/1.1 200 OK\r\n...", ...)             = 172 ← 收到响应
recvfrom(4, "...", ...)                                = 754 ← 继续收
close(4)                                                     ← 关闭连接
```

注意：
- curl 先用 `connect` 连到本地代理（端口 7897），再发 HTTP 请求
- `sendto` = strace 对 `write/send` 的显示
- `recvfrom` = strace 对 `read/recv` 的显示
- 两次 `recvfrom` 说明响应数据分两个 TCP 段到达——**这就是为什么需要循环 read**

**curl 和我们的 echo server 对比**：

| | echo server | curl |
|---|---|---|
| `socket()` | ✅ 手动写 | ✅ 自动 |
| `connect()` | ❌（server 不需要） | ✅ 自动 |
| `bind()` | ✅ 手动写 | ❌（client 不需要） |
| `listen()` | ✅ 手动写 | ❌ |
| `accept()` | ✅ 手动写 | ❌ |
| `sendto` / `recvfrom` | ✅ read/write | ✅ 自动 |

**关键认识**：curl 底层和我们写的 echo client 用的是**完全相同的系统调用**。curl 只是在这些系统调用之上实现了 HTTP 协议。

---

## 任务三：多连接困境

### 实验

```bash
# 编译
gcc -Wall -g -O0 -o blocking-server blocking-server.c

# 终端1：启动 server
./blocking-server

# 终端2：连接第一个客户端
nc localhost 8080

# 终端3：连接第二个客户端
nc localhost 8080

# 终端4：连接第三个客户端
nc localhost 8080
```

### 观察结果

| 终端 | nc 状态 | 为什么 |
|------|---------|--------|
| 终端2 | **已连接，正在等待输入** | `accept()` 返回了，现在停在 `read()` |
| 终端3 | **已连接，但什么也做不了** | `accept()` 返回了（在 backlog 里排到队了），但 server 卡在上一个 `read()` 里 |
| 终端4 | **已连接，同样等** | 同上 |

**等等——终端3和终端4居然连上了？**

这是因为下面的实验设计细节：`blocking-server.c` 在 `read()` 返回后就 `close(client_fd)` 然后回到 `accept()`，所以终端2 如果直接回车发送数据，server 就会处理它并关闭，然后 accept 终端3。

**真正的问题在另一个场景**：如果你**不修改** `blocking-server.c`，而是让**终端2 不发送任何数据**——保持连接但不发数据：

```
Server                            Client A                Client B
  │                                 │                       │
  │ accept() ← 返回 client_fd_A      │                       │
  │                                 │                       │
  │ read(client_fd_A)  ← 阻塞等待    │                       │
  │    (A 不发数据，一直卡在这里)     │  nc localhost 8080     │
  │                                 │  (连着但不打字)        │
  │                                 │                       │ nc localhost 8080
  │                                 │                       │ → 在 backlog 排队
  │                                 │                       │
  │ 永远回不到 accept()!!!           │                       │ 永远连不上!!!
```

### 为什么单线程阻塞模型处理不了多客户？

一张图说清楚：

```
单线程阻塞模型

  accept() ──→ 等 Alice
       │
       ▼
  read(Alice) ──→ 等 Alice 说话
       │              │
       │         Alice 在发呆...
       │         谁也不回
       │              │
       ▼              ▼
  Bob 想连接 ──→ 但 server 卡在 read(Alice) 里，根本没回 accept()
  Carol 想连接 ──→ 同理

问题本质：
  "接线员只有一个，而且每次只接一条线"
  "接了 Alice 的线之后，接线员就一直等 Alice 说话——不挂断，也不接新电话"
```

### 解决方案预告（W4 内容）

| 方案 | 怎么解决 | 复杂度 |
|------|---------|--------|
| **多进程** `fork()` | 每个客户端 fork 一个子进程 | 开销大，C10K 不适用 |
| **多线程** `pthread` | 每个客户端一个线程 | 线程栈开销，竞态复杂 |
| **非阻塞 + epoll** | 一个线程管所有 fd，谁有数据处理谁 | ✅ 这就是我们要学的 |

**W4 你会学到 epoll**——让你一个线程同时处理成千上万个连接。它是 Nginx、Redis 等高性能服务的基石。

---

## 本周核心收获

1. **`read_full` / `write_full`** 是写网络程序的基本功 —— 永远假设部分读/写
2. **strace** 让你看到程序的"真相" —— 每个系统调用、每个返回值、每次阻塞都能看到
3. **阻塞 I/O 的致命缺陷** —— 一个线程 + 阻塞 = 一次只能服务一个客户端
4. **TCP 是字节流** —— 没有消息边界，粘包/拆包是你的责任
