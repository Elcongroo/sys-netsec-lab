# W3 验收考察

## 第 1 题（20 分）

下面这段 strace 输出是跟踪一个 echo server 得到的。请逐行解释每一行在做什么：

```
socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) = 3
bind(3, {sa_family=AF_INET, sin_port=htons(8080), sin_addr=inet_addr("0.0.0.0")}, 16) = 0
listen(3, 5) = 0
accept(3, {sa_family=AF_INET, sin_port=htons(56789), sin_addr=inet_addr("127.0.0.1")}, [16]) = 4
recvfrom(4, "hello\n", 4096, 0, ...) = 6
sendto(4, "hello\n", 6, 0, ...) = 6
close(4) = 0
```

重点回答：
1. fd=3 和 fd=4 分别扮演什么角色？
2. `listen(3, 5)` 里的 5 是什么含义？
3. 这 7 行中，哪几行是**阻塞**的（会等待）？

---

## 第 2 题（20 分）

你写了一个 echo client，代码如下：

```c
char send_buf[] = "hello_world";
char recv_buf[1024];

write(sock_fd, send_buf, 11);      // 发送 11 字节
ssize_t n = read(sock_fd, recv_buf, 1024);  // 期望读回 11 字节
```

请回答：
1. 这段代码有个隐含的 Bug。Hint：跟 `read` 的返回值有关。
2. 如果你不得不一次 `read` 就期望拿到完整的 11 字节，服务器端应该做什么保证？（一句代码回答）
3. 写出修复这个 client 的方式（用 `read_full` 的思路，只写伪代码即可）

---

## 第 3 题（20 分）

你在做任务三时，用 `blocking-server.c` 实验。终端 2 的 nc 连着但不发任何数据，终端 3 的 nc 也连接了，终端 4 的 nc 尝试连接。

1. 终端 3 能连上吗？为什么？
2. 终端 3 连上后能收到 echo 吗？为什么？
3. 假设 `backlog` 设为 1（`listen(fd, 1)`）。终端 4 的 nc 会怎样？
4. 用你自己的话，画一张图或文字解释：为什么单线程阻塞模型处理不了多个客户端？

---

## 第 4 题（20 分）

请用自己的话解释以下概念，可以用类比，不需要背定义：

1. 文件描述符（fd）是什么？为什么说是"一切皆文件"？
2. `htons()` 做了什么？为什么需要它？
3. `strace` 能告诉你什么信息？当一个程序卡住不动了，你第一反应是什么？
4. `INADDR_ANY` 是什么？什么时候用？什么时候不用？

---

## 第 5 题（20 分）

我们的 echo server 目前只支持一个线程。请综合 W2 + W3 学到的知识，回答：

1. 如果 server 处理一个请求时 `malloc(32)` 了一个 buffer，但在某个错误路径上忘了 `free`——这种 Bug 会立刻让程序崩溃吗？用什么工具能查到？
2. 如果 server 因为某种原因连续 3 天不重启，但第 3 天凌晨突然 SIGSEGV 了——这是我们在 W2 学过的哪种 Bug 模式？可能的原因是什么？
3. W4 要学的 epoll 用来解决我们 W3 发现的什么问题？（一句话回答）
