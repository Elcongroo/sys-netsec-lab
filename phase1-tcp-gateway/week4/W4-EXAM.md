# W4 验收考察：epoll I/O 多路复用

> **考察日期**：2026-07-31
> **覆盖范围**：W4 教程 + 三个任务
> **满分**：100 分
> **规则**：先独立作答，再提交给我批改。可以翻教程、查 man page、看自己的代码，但不能让我直接给答案。

---

## 一、概念理解（每题 10 分，共 30 分）

### Q1：为什么需要 epoll？（10分）

W3 你写了一个阻塞 echo server（`blocking-server.c`），它一次只能服务一个客户端。

请用你自己的话解释：
1. 阻塞模型的致命问题是什么？（3分）
2. epoll 是如何解决这个问题的？（3分）
3. 为什么不能在阻塞模型里用"多线程"来替代 epoll？（从 fd 数量角度回答）（4分）

---

### Q2：LT 和 ET 的本质区别（10分）

假设一个客户端发了 500 字节数据，但你的程序只 `read` 了 200 字节，缓冲区还剩 300 字节。

1. 在 LT（水平触发）模式下，`epoll_wait` 下次会怎样？（3分）
2. 在 ET（边缘触发）模式下，`epoll_wait` 下次会怎样？（3分）
3. 如果 ET 模式下你不读完会有什么后果？（4分）

---

### Q3：epoll 比 select 好在哪里？（10分）

面试官问你："我们有个服务有 10000 个并发连接，为什么不能用 select？"

请从三个维度回答：
1. fd 数量限制（3分）
2. 每次调用的开销（4分）
3. 查找就绪 fd 的开销（3分）

---

## 二、代码阅读（每题 15 分，共 30 分）

### Q4：这段代码有什么问题？（15分）

```c
// 某同学写的 epoll echo server 片段
int epfd = epoll_create1(0);
struct epoll_event ev;
ev.events = EPOLLIN;
ev.data.fd = listen_fd;
epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);

struct epoll_event events[64];

while (1) {
    int nfds = epoll_wait(epfd, events, 64, -1);

    for (int i = 0; i < nfds; i++) {
        int fd = events[i].data.fd;

        if (fd == listen_fd) {
            int cfd = accept(listen_fd, NULL, NULL);
            // 注意：这里没有设非阻塞，也没有注册到 epoll
            char buf[1024];
            int n = read(cfd, buf, sizeof(buf));
            write(cfd, buf, n);
            close(cfd);
        }
    }
}
```

请找出至少 **3 个问题**，对每个问题说明：
- 问题是什么？
- 它会导致什么后果？

---

### Q5：状态机为什么需要 `send_done` 字段？（15分）

在你的 `epoll-stateful.c` 中，`conn_t` 结构体有一个 `send_done` 字段。

```c
typedef struct {
    // ...
    char   send_buf[BUF_SIZE];
    size_t send_len;        // 待发送总字节数
    size_t send_done;       // 已发送字节数
} conn_t;
```

请回答：
1. 为什么不能直接用 `write(fd, send_buf, send_len)` 然后假设全写完了？（5分）
2. 在非阻塞模式下，`write` 返回 -1 且 `errno == EAGAIN` 是什么意思？（5分）
3. 如果你在 `do_write` 里遇到了 EAGAIN，下一步应该怎么处理？（5分）

---

## 三、设计题（每题 20 分，共 40 分）

### Q6：设计一个简单的 HTTP 请求解析器（20分）

你的 `epoll-stateful.c` 目前是"读到 `\n` 就开始 echo"。现在要求把它改造成一个**简单的 HTTP 请求解析器**。

HTTP 请求的格式是：
```
GET /index.html HTTP/1.1\r\n
Host: example.com\r\n
\r\n
```

即：请求以 `\r\n\r\n`（两个连续 CRLF）标记结束。

请设计你的状态机改造方案：
1. 状态转换图（5分）
2. `conn_t` 需要增加哪些字段？（5分）
3. 描述 `do_read` 的改造逻辑——你怎么判断"请求头收完了"？（5分）
4. 收到完整请求后应该怎么响应？（写一个最小的 HTTP 响应）（5分）

---

### Q7：ET 模式改造方案（20分）

你现在写的 `epoll-echo.c` 用的是 LT（默认）模式。现在要求你把它改成 ET 模式。

请回答：
1. 除了把 `EPOLLIN` 改成 `EPOLLIN | EPOLLET`，还需要改什么？（5分）
2. 为什么 ET 模式下必须循环 `read` 直到返回 `EAGAIN`？（5分）
3. 如果循环 read 时一直有数据（恶意客户端疯狂发包），会发生什么？怎么防止？（5分）
4. 写出 ET 模式下正确的 `read` 循环伪代码（5分）

---

## 评分标准

| 分数段 | 等级 | 含义 |
|--------|------|------|
| 90-100 | S | 完全掌握，可以进入 W5 |
| 75-89 | A | 理解核心概念，个别细节需补 |
| 60-74 | B | 基本理解，需回顾部分内容 |
| < 60 | C | 核心概念未掌握，建议重做关键实验 |

---

**准备好了就开始作答。你可以直接在下面写答案，也可以新建一个 `W4-EXAM-ANSWERS.md` 文件。**
