# W3：I/O 系统调用与 Socket 编程基础

> **你的位置**：Phase 1 — 系统基础与 TCP 网关  
> **本周目标**：理解文件描述符、系统调用、阻塞 I/O，写出第一个 TCP echo server  
> **预估时间**：理论 2h + 实验 4h + 编码 6h

---

## 为什么学这个？

你已经知道内存怎么管理、Bug 怎么排查。但到目前为止，你写的程序都是"孤独的"——读读文件、算算数据，运行完就退出了。

真正的服务器不一样。它**等着外部世界来连接它**，同时处理成千上万个客户端。要做到这一点，你需要理解操作系统提供的**I/O 机制**——程序怎么"等"数据？怎么同时处理多个连接？为什么有时候程序明明在等，CPU 却是 0%？

这些问题的答案，都在本周要学的三个概念里：**文件描述符、系统调用、阻塞 I/O**。

---

## 第一阶段：文件描述符（File Descriptor）

### 1.1 一切都是文件

Unix 的设计哲学：**Everything is a file**。

什么意思？不管是磁盘上的真实文件、网络连接、管道、终端，操作系统都用一个统一的概念来表示——**文件描述符**。

```
open("data.txt") → fd=3  → [文件系统中的一个 inode]
socket()        → fd=4  → [内核中的 TCP socket 数据结构]
pipe()          → fd=5  → [内核中的管道缓冲区]
```

对程序员来说，打开文件、接受网络连接、创建管道之后，拿到的都是一个 **int 类型的数字**。后续的 read / write / close 操作，对这个 int 做就行——不需要知道它底层是什么。

### 1.2 fd 的数字是怎么分配的

每个进程有一个**文件描述符表**，存在内核中。fd 就是这个表的索引：

```
进程的文件描述符表（简化）：
┌─────┬─────────────────────────────┐
│  0  │  stdin  (标准输入，键盘)       │
│  1  │  stdout (标准输出，屏幕)       │
│  2  │  stderr (标准错误输出，屏幕)    │  ← 这三个是每个进程自带的
│  3  │  data.txt                    │  ← open() 返回的
│  4  │  TCP connection to client    │  ← accept() 返回的
│  5  │  pipe read end               │  ← pipe() 返回的
│ ... │                              │
└─────┴─────────────────────────────┘
```

**规则**：新 fd 永远是当前可用的**最小整数**。这就是为什么 `open()` 返回 3——0、1、2 已经被 stdin/stdout/stderr 占了。

### 1.3 亲手验证：看自己的 fd

```c
// demo-fd-table.c
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main() {
    printf("stdin=%d, stdout=%d, stderr=%d\n", STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO);

    int fd1 = open("/etc/hostname", O_RDONLY);
    printf("fd1=%d\n", fd1);

    int fd2 = open("/etc/hosts", O_RDONLY);
    printf("fd2=%d\n", fd2);

    close(fd1);
    int fd3 = open("/etc/passwd", O_RDONLY);
    printf("fd3=%d (应该等于 fd1=%d，因为最小的空缺被重用了)\n", fd3, fd1);

    close(fd2);
    close(fd3);
    return 0;
}
```

运行它，你会看到 `fd3` 确实等于之前的 `fd1`。

---

## 第二阶段：系统调用（System Call）

### 2.1 用户态和内核态

你的 C 程序运行在**用户态**——它不能直接操作硬件、不能直接访问磁盘、不能直接发送网络包。

当你的程序想做这些"特权操作"时，必须通过**系统调用**——一条特殊的 CPU 指令（`syscall`），让 CPU 从用户态切换到内核态，内核帮你把事情干了，再切回来。

```
你的程序：
  read(fd, buf, 1024)
    ↓
  用户态 → syscall 指令 → 内核态
    ↓
  内核：从 socket 缓冲区拷贝数据到你的 buf
  内核：更新文件位置指针
    ↓
  内核态 → sysret 指令 → 用户态
    ↓
  read() 返回（告诉你读了多少字节）
```

### 2.2 用 strace 偷看系统调用

`strace` 是一个工具，它能**显示程序调用的每一个系统调用**。这是理解程序行为最强大的工具之一。

```bash
# 追踪最简单的程序
strace echo hello 2>&1 | head -20

# 你会看到类似这样的输出：
# execve("/usr/bin/echo", ...)     ← 启动程序
# write(1, "hello\n", 6)          ← 向 fd=1（stdout）写 "hello\n"
# exit_group(0)                    ← 退出
```

**strace 的核心价值**：不需要看源码，就能看到程序"在操作系统的眼里"做了什么。

### 2.3 read() 和 write() 的返回值

```c
ssize_t read(int fd, void* buf, size_t count);
ssize_t write(int fd, const void* buf, size_t count);
```

`read` 很重要的一点：**返回值不一定是 count**。

```
你请求读 1024 字节，但当前只有 300 字节可读
  → read 返回 300（而不是阻塞等待 1024 字节）

你请求读 1024 字节，但客户端发送了 "hello\n"（6 字节）就停了
  → read 返回 6

你请求读 1024 字节，但客户端关闭了连接
  → read 返回 0（EOF）
```

**关键认知**：TCP 是一个**字节流**。没有"消息边界"。客户端写了 100 字节，你可能一次 read 就全读到，也可能分 3 次 read 才读完。你的代码**必须**处理这种情况。

---

## 第三阶段：TCP Socket API 全景

### 3.1 Socket 是什么

Socket 不是协议，是**编程接口**。内核里的 TCP 实现你可以通过 socket API 来用。

```c
#include <sys/socket.h>
int socket(int domain, int type, int protocol);
//         AF_INET     SOCK_STREAM    0
//          IPv4       TCP
```

`socket()` 返回的是一个 fd——和 `open()` 返回的东西用起来一样。这就是"一切皆文件"的力量。

### 3.2 Server 端：五步创建

```c
// Step 1: 创建 socket
int listen_fd = socket(AF_INET, SOCK_STREAM, 0);

// Step 2: 绑定地址（"我在 0.0.0.0:8080 等连接"）
struct sockaddr_in addr = {
    .sin_family = AF_INET,
    .sin_port   = htons(8080),      // htons: host to network short（字节序转换）
    .sin_addr.s_addr = INADDR_ANY,   // 监听所有网卡
};
bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr));

// Step 3: 开始监听（"操作系统，帮我排队的任务交给你了"）
listen(listen_fd, 128);  // backlog = 128: 最多 128 个连接在队列里等

// Step 4: 接受连接（阻塞等待）
struct sockaddr_in client_addr;
socklen_t client_len = sizeof(client_addr);
int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);

// Step 5: 通信
char buf[1024];
ssize_t n = read(client_fd, buf, sizeof(buf));
write(client_fd, buf, n);  // echo 回去
close(client_fd);
```

### 3.3 关键理解：两个 fd

```
listen_fd  — 只做一件事：接电话
             bind+listen 之后它就"专职"等待新连接

client_fd  — 每个客户端连接一个
             accept() 返回的新 fd，用它来和这个具体的客户端通信
```

```
                          listen_fd (一直存活，等电话)
                          │
    connect() ───────────►│ accept()
    客户端                  │
                          ├──► client_fd #1 (Alice)
    connect() ───────────►│
                          ├──► client_fd #2 (Bob)
    connect() ───────────►│
                          └──► client_fd #3 (Carol)
```

### 3.4 Client 端：两步连接

```c
int sock_fd = socket(AF_INET, SOCK_STREAM, 0);

struct sockaddr_in server_addr = {
    .sin_family = AF_INET,
    .sin_port   = htons(8080),
};
inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);  // 字符串 → 网络地址

connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));

// 连接成功！sock_fd 现在就是一条通往 server 的"管道"
write(sock_fd, "hello", 5);
char buf[1024];
read(sock_fd, buf, sizeof(buf));  // 读回 server 的响应
```

---

## 第四阶段：阻塞 I/O ——最简单的模型

### 4.1 什么叫阻塞

```c
int client_fd = accept(listen_fd, NULL, NULL);  // 没有客户端连接 → 阻塞
ssize_t n = read(client_fd, buf, 1024);          // 客户端没发数据 → 阻塞
```

**阻塞**的意思：程序停在这一行，**CPU 不消耗**，操作系统把当前线程挂起。等事件发生了（有连接来了/有数据到了），操作系统把它唤醒，程序继续往下走。

这就是为什么你的 echo server 在"等连接"时 CPU 是 0%——它不是在忙等，而是真的"睡着了"。

### 4.2 阻塞模型的致命问题

```c
// 假设你只有一个线程
int client1 = accept(listen_fd, ...);  // 这行返回后，拿到了 Alice
char buf[1024];
read(client1, buf, 1024);              // Alice 在发呆，没发数据 → 阻塞！
                                        // Bob 想连接，但是没人 accept → Bob 连不上
                                        // Carol 发来了数据，但是没人 read → 数据堆在缓冲区
```

**一个客户端卡住，所有其他客户端都被卡住。** 这就是单线程阻塞模型的致命问题。

## 第五阶段：源码实验

### 5.1 Echo Server（最简单的 TCP 服务器）

编译运行后，用另一个终端 `nc localhost 8080` 连接，输入任意文本，server 会 echo 回来。

### 5.2 用 strace 看系统调用

```bash
# 先在一个终端启动 server
./echo-server

# 在另一个终端找它的 PID
ps aux | grep echo-server

# 用 strace 跟踪
strace -p <PID> -f -e trace=network 2>&1
```

然后用 `nc localhost 8080` 连接，看 strace 输出——你会看到 `socket`、`bind`、`listen`、`accept`、`recvfrom`、`sendto` 全套系统调用。

---

## 核心概念速查

| 概念 | 一句话 |
|------|--------|
| 文件描述符（fd） | 一个 int，代表打开的文件/socket/管道 |
| 系统调用 | 用户程序请求内核帮忙的唯一方式 |
| strace | 偷看程序调用什么系统调用的工具 |
| listen_fd vs client_fd | 一个是"接线员"，一个是"通话线路" |
| 阻塞 | 程序停在某行，不消耗 CPU，等待事件发生 |
| htons / ntohs | 字节序转换（x86 小端 ↔ 网络大端） |
| backlog | listen 的第二个参数，内核里等待队列的最大长度 |

---

## W3 任务

### 任务一：Echo Server + Client（编码 A，~3h）

1. 写一个 TCP echo server，监听 `0.0.0.0:8080`
2. 写一个 TCP echo client，连接到 server
3. 要求处理**部分读**（partial read）——不要假设一次 read 就读完所有数据
4. 用 strace 跟踪 server，截图关键系统调用

### 任务二：strace 实验（实验，~1h）

1. 用 strace 跟踪 `cat /etc/hostname`
2. 用 strace 跟踪 `curl http://example.com 2>&1 | head -5`
3. 在你的笔记里记录：每个命令调用了哪几个你认识的系统调用？

### 任务三：多连接困境（编码 B，~2h）

1. 修改你的 echo server，让它永远不会退出（while(1) accept + read）
2. 同时打开 3 个 `nc localhost 8080`，观察——第二个、第三个客户端能不能连上？能不能收到 echo？
3. 用一张图或文字解释：为什么单线程阻塞模型处理不了多个客户端？
