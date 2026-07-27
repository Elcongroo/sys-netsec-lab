# W3：I/O 系统调用与 Socket 编程基础

> **你的位置**：Phase 1 — 系统基础与 TCP 网关  
> **本周目标**：理解文件描述符、系统调用、阻塞 I/O，写出第一个 TCP echo server  
> **预估时间**：理论 2h + 实验 4h + 编码 6h

---

## 为什么学这个？

你已经知道内存怎么管理、Bug 怎么排查。但到目前为止，你写的程序都是"孤独的"——读读文件、算算数据，运行完就退出了。

真正的服务器不一样。它**等着外部世界来连接它**，同时处理成千上万个客户端。要做到这一点，你需要理解操作系统提供的**I/O 机制**——程序怎么"等"数据？怎么同时处理多个连接？为什么有时候程序明明在等，CPU 却是 0%？

---

## 本教程的阅读方式

每个新概念都按**三层**组织：

| 层 | 标题 | 回答的问题 |
|---|------|-----------|
| ① | 为什么需要它？ | 没有这个东西会怎样？它解决什么问题？ |
| ② | 它是什么？ | 一句话定义 + 类比 |
| ③ | 怎么用？ | 最小可运行代码 + 逐行解释 |

**每个代码块之前，会先列出你即将看到的新符号**——不需要背，知道它是干什么的就行。

---

## 第一阶段：文件描述符（File Descriptor）

### 1.1 为什么需要它？

你的程序想读一个文件、发一个网络包、从键盘读一行输入。这三件事看起来完全不同——但操作系统希望**用统一的方式来管理它们**。否则你需要三套不同的 API，程序会非常复杂。

### 1.2 它是什么？

**一句话**：文件描述符是一个 `int` 数字，代表一个"打开的东西"。这个东西可以是磁盘文件、socket 连接、管道、终端——都可以。

```
你的程序                内核
  │                      │
  │  open("a.txt")       │
  │ ──────────────────►  │  在进程的 fd 表里登记一项
  │  返回: 3             │  fd=3 → a.txt 的 inode
  │                      │
  │  read(3, buf, 100)   │
  │ ──────────────────►  │  找到 fd=3 → a.txt → 读磁盘
  │  返回: 100           │
```

**类比**：你去银行办事，柜员给你一个**号码牌**。不管你是存款、取款、开户，柜员都认这个号码。fd 就是这个号码牌。

### 1.3 怎么用？

所有 I/O 操作都用一个 fd 数字：

```c
int fd = open("file.txt", O_RDONLY);  // → fd 是 3
read(fd, buf, 100);                    // 从 fd=3 读
close(fd);                             // 用完了，还回去
```

**fd 的分配规则**：永远分配**当前可用的最小整数**。

每个进程启动时都自带三个 fd：

| fd | 名字 | 是什么 |
|----|------|--------|
| 0 | stdin | 键盘输入 |
| 1 | stdout | 屏幕输出 |
| 2 | stderr | 错误输出 |

所以你自己 `open()` 的第一个文件拿到的 fd 是 **3**。关掉 fd=3 之后再开一个，拿到的还是 3——因为 3 成了"空缺"。

**验证实验**：编译运行 `demo/fd-table.c`，亲眼看到这个规则生效。

---

## 第二阶段：系统调用（System Call）

### 2.1 为什么需要它？

你的 C 程序运行在**用户态**——不能直接操作硬盘、不能直接发网络包、不能直接分配真实内存页。这些"特权操作"必须由**内核**来做。

但你的程序怎么叫内核帮忙？不能直接调内核的函数——内核在另一个地址空间里。你需要一条**特殊的 CPU 指令**来"穿越"这道边界。

### 2.2 它是什么？

**一句话**：系统调用是你的程序**请求内核帮忙**的唯一方式。

整个过程：

```
你的程序:  read(fd, buf, 1024)
              │
              ▼
          glibc 包装函数（普通的 C 函数调用）
              │
              ▼
          syscall 指令（CPU 从用户态切到内核态）
              │
              ▼
          内核: 找到 fd → socket 缓冲区 → 拷贝数据到 buf
              │
              ▼
          sysret 指令（CPU 切回用户态）
              │
              ▼
          read() 返回: "读了 300 字节"
```

你平常写的 `read()`、`write()`、`open()`、`malloc()` ——这些 glibc 函数底层**全部**通过系统调用实现。glibc 只是帮你包装了一下，让你用起来更方便。

### 2.3 怎么观察系统调用？

用 **strace**——它能**显示程序调用的每一个系统调用**。不需要看源码，就能看到程序"在操作系统的眼里"做了什么。

```bash
# 跟踪最简单的程序
strace echo hello 2>&1 | head -10
```

你会看到类似这样的输出：

```
execve("/usr/bin/echo", ["echo", "hello"], ...) = 0    ← 启动程序
write(1, "hello\n", 6)                          = 6    ← 向 fd=1（stdout）写 6 字节
exit_group(0)                                         ← 退出
```

每一行的格式：
```
系统调用名(参数...)  =  返回值
```

**strace 的核心价值**：当程序卡住了、报错了、行为诡异——strace 告诉你它在等哪个系统调用、在哪个 fd 上等。这是排障的第一把钥匙。

### 2.4 read() 和 write() 的返回值——非常重要的细节

```c
ssize_t read(int fd, void* buf, size_t count);
//               │           │          │
//               │           │          └── 我"请求"读多少字节
//               │           └── 读到哪
//               └── 从哪个 fd 读
//    返回值: 实际读到的字节数
```

**关键认知**：`read()` 的返回值**不一定是 count**。

```
你请求: read(fd, buf, 1024)   ← "给我 1024 字节"
内核:    buf 里现在只有 300 字节可读
         → 返回 300（而不是等满 1024 字节）

你请求: read(fd, buf, 1024)
内核:    客户端发了 "hello"（5 字节）就停了
         → 返回 5

你请求: read(fd, buf, 1024)
内核:    客户端关闭了连接
         → 返回 0（EOF = End Of File = 对端关闭）
```

**TCP 是一个字节流，没有"消息边界"**。对方写了 100 字节，你可能一次 `read` 就全读到，也可能分 3 次 `read` 才读完。你的代码**必须**处理这种情况——这就是任务一的核心要求。

---

## 第三阶段：Socket 是什么？

在进入"怎么用 Socket API"之前，我们先把概念厘清。

### 3.1 为什么需要 Socket？

你要在两台机器之间通信。但两台机器之间没有"文件"可打开——你需要一种**新的东西**来表示网络连接。同时，你希望这个新东西用起来**和文件差不多**（用 fd 表示，用 read/write 操作）。

Socket 就是为此设计的——它是**文件模型的网络扩展**。

### 3.2 它是什么？

**一句话**：Socket 是**网络连接的文件描述符**。

```
文件:   open("data.txt") → fd=3  →  read(3, ...)  →  读磁盘数据
Socket: socket()         → fd=4  →  read(4, ...)  →  读网络数据
```

创建它、读它、写它、关它——和文件操作用**同一套 API**。这就是"一切皆文件"的设计哲学。

### 3.3 Server 和 Client 的区别

```
Client                               Server
  ① socket()                          ① socket()
  ② connect(server的地址)              ② bind(自己的地址)
  ③ write() / read()                  ③ listen() ← "我开始接电话"
                                       ④ accept() ← 等 client 来连
                                       ⑤ read() / write()
```

Server 比 Client 多了两步 `bind` + `listen`——因为 Server 需要**公布自己的地址**让 Client 能找到。

---

## 第四阶段：一步一步写出 Echo Server

现在开始写代码。但**不是一次性扔出 30 行代码**——我们一步步来，每一步只引入一个概念。

### 在开始之前：你需要知道的两个新东西

本阶段会遇到以下新符号。先看一眼，用到的时候再回来看：

| 符号 | 一句话解释 |
|------|-----------|
| `AF_INET` | "我用 IPv4 地址"（AF = Address Family） |
| `SOCK_STREAM` | "我用 TCP"（流式、可靠、有连接） |
| `struct sockaddr_in` | 一个结构体，装"IPv4 地址 + 端口" |
| `htons(8080)` | 把 8080 转成网络字节序（大端） |

### Step 1：创建 socket ——就一行

```c
int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
//                     │         │            │
//                     │         │            └── 协议（0 = 根据 type 自动选）
//                     │         └── 类型 = "流式"（TCP）
//                     └── 地址族 = IPv4
```

**返回值是一个 fd**——和 `open()` 一样。

### Step 2：绑定地址 ——"我住这里"

Server 必须告诉操作系统："我在哪个端口等连接？"

```c
struct sockaddr_in addr = {
    .sin_family = AF_INET,          // IPv4
    .sin_port   = htons(8080),      // 端口 8080（网络字节序）
    .sin_addr.s_addr = INADDR_ANY,   // 监听所有网卡（0.0.0.0）
};

bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr));
```

**逐行解释**：

- `struct sockaddr_in`：一个结构体，装 **IPv4 地址 + 端口**。里面有 4 个字段，你只需要关注 3 个：
  - `sin_family`：填 `AF_INET`，表示"这是 IPv4"
  - `sin_port`：端口号。为什么用 `htons()`？因为网络协议规定端口号必须用**大端字节序**（高位在前），而你的 x86 CPU 是小端（低位在前）。`htons` = **H**ost **to** **N**etwork **S**hort ——把主机字节序转成网络字节序。
  - `sin_addr.s_addr`：IP 地址。`INADDR_ANY` = `0.0.0.0`，意思是"所有网卡都监听"

- `bind()`：把这个地址"绑定"到 fd 上。相当于告诉操作系统："发到 0.0.0.0:8080 的包，都给我这个 socket"

### Step 3：开始监听 ——"我准备好接电话了"

```c
listen(listen_fd, 128);
//                 │
//                 └── backlog: 最多多少个连接在队列里排队等候
```

`listen()` 之后，你的 socket 从"普通 socket"变成了**监听 socket**——它只做一件事：**等连接**。这个 socket 不读数据、不写数据——它纯粹是"接线员"。

### Step 4：接受连接 ——"你好，哪位？"

```c
struct sockaddr_in client_addr;
socklen_t client_len = sizeof(client_addr);

int client_fd = accept(listen_fd,                        // 从哪个"接线员"接
                       (struct sockaddr*)&client_addr,   // 把对方的地址写到这里
                       &client_len);                     // 地址的长度

// accept() 阻塞！直到有客户端连接才返回
```

`accept()` 返回的是**一个新的 fd**——`client_fd`。这个 fd 才真正用来 read/write。

**这是初学者最容易混淆的点——两个 fd：**

```
listen_fd   = 接线员（一直存在，专职等电话）
client_fd   = 通话线路（每个客户一条，用来收发数据）
```

### Step 5：收发数据 ——"请讲"

```c
char buf[1024];
ssize_t n = read(client_fd, buf, sizeof(buf));
//               │           │
//               │           └── 普通 int fd，和读文件一样
//               └── 这是 accept() 返回的那个 client_fd

write(client_fd, buf, n);   // echo 回去
close(client_fd);            // 通话结束，挂断
```

**注意**：这里不是 `read(listen_fd)`——listen_fd 只用来 accept，不读数据。

### 完整流程图

```
listen_fd                              client_fd
   │                                      │
   │ bind + listen                        │
   │                                      │
   ├── accept() ──→ 返回 client_fd ──────→ │
   │                                      │
   │                              read(client_fd) / write(client_fd)
   │                                      │
   │                              close(client_fd)
   │
   ├── accept() ──→ 又是一个新的 client_fd（下一个客户端）
   │
   ...
```

---

## 第五阶段：阻塞 I/O ——最简单的模型

### 5.1 什么叫阻塞？

```c
accept(listen_fd, ...)    // 没有客户端连接 → 程序停在这里等
read(client_fd, buf, 1024) // 客户端没发数据 → 程序停在这里等
```

**阻塞**的意思是：程序停在当前行，CPU 不消耗——操作系统把线程**挂起**（睡眠）。等数据来了，操作系统把它唤醒，程序继续往下走。

这就是为什么你的 echo server 在"等连接"时 CPU 是 0%——它不是忙等，是真的睡着了。

### 5.2 阻塞模型的致命问题

假设你的 echo server 只有一个线程，处理完 Alice 才能处理 Bob：

```c
while (1) {
    int client = accept(listen_fd, ...);  // Alice 连上了 → 返回
    read(client, buf, 1024);              // Alice 在发呆，没发数据 → 阻塞在这里！
                                          // Bob 想连，但 accept() 没被调用 → Bob 连不上
                                          // Carol 发了数据，但没人 read → 数据堆在内核缓冲区
}
```

**一个客户端卡住，所有其他客户端都被卡住。** 这就是单线程阻塞模型的致命缺陷。

这也是为什么后面需要 **epoll**——在 W4 我们专门学它。

---

## 核心概念速查

| 概念 | 一句话 |
|------|--------|
| 文件描述符（fd） | 一个 int，代表打开的文件/socket/管道 |
| 系统调用 | 用户程序请求内核帮忙的唯一方式 |
| strace | 偷看每个系统调用的工具 |
| `htons()` | 把主机字节序转为网络字节序（端口号必须用） |
| `struct sockaddr_in` | 装 IPv4 地址 + 端口的结构体 |
| `listen_fd` | 接线员——只 accept，不 read |
| `client_fd` | 通话线路——每个客户端一条 |
| 阻塞 | 线程挂起不耗 CPU，等事件 |
| backlog | `listen()` 的第二个参数，等待队列的最大长度 |

---

## W3 任务

### 任务一：Echo Server + Client（编码 A，~3h）

1. 写一个 TCP echo server，监听 `0.0.0.0:8080`
2. 写一个 TCP echo client，连接到 server
3. **必须**处理部分读（partial read）——不要假设一次 read 就读完所有数据。提示：用循环 `while (total < expected)` 的方式读
4. 用 strace 跟踪 server，截图关键系统调用

### 任务二：strace 实验（实验，~1h）

1. 用 strace 跟踪 `cat /etc/hostname`，记录使用到的主要系统调用
2. 用 strace 跟踪 `curl http://example.com`，记录和网络相关的系统调用
3. 在你的笔记里整理：哪些系统调用你认识？哪些不认识？

### 任务三：多连接困境（编码 B，~2h）

1. 修改 echo server，让它用 `while(1)` 循环 accept → read → write → close
2. 同时打开 3 个 `nc localhost 8080`，观察——第二个、第三个客户端能不能连上？能不能收到 echo？
3. 用一张图或文字解释：为什么单线程阻塞模型处理不了多个客户端？
