# W5 审查任务：TCP 代理代码审查

> **模式**：这不是"写代码"任务，而是"审查 AI 生成的代码"任务。
> 你的目标：看懂每一块代码 → 讲出它为什么这么设计 → 找出潜在问题。

---

## 审查维度

| 维度 | 你审什么 |
|------|---------|
| 设计决策 | 为什么用这个结构？不用会怎样？ |
| 正确性 | 这段逻辑有没有 bug？边界情况对吗？ |
| 安全性 | 有没有资源泄漏？断开时清理干净了吗？ |

---

## 第一部分：设计决策（口述）

打开 `demo/tcp-agent.c`，找到以下位置，用自己的话回答：

### D1：proxy_conn 结构体（第 75-91 行）

**为什么需要 `backend_ready` 字段？** 代码里连完后端就立即注册了 `EPOLLIN`，但连接可能还没建好。这个字段在代码里被赋值了却从来没被检查过。这是 bug 还是冗余字段？

### D2：两个方向的缓冲区（第 83-91 行）

`c2b_buf` 和 `b2c_buf` 在 `proxy_conn` 里分配了，但 `forward()` 函数**完全没用它们**——它用的是一个栈上的 `char buf[BUF_SIZE]`。这两个缓冲区是多余的吗？什么场景下它们必须存在？

### D3：g_conns 全局数组 vs data.ptr（第 100 行 vs 第 263 行）

代码同时用了 `g_conns[fd % 1024]`（按 fd 查表）和 `ev.data.ptr`（把指针绑进事件）。为什么两个都要？能不能只用其中一个？

---

## 第二部分：正确性审查（口述）

### C1：forward 函数（第 153-167 行）

```c
static int forward(int src_fd, int dst_fd) {
    char buf[BUF_SIZE];
    ssize_t n = read(src_fd, buf, sizeof(buf));
    if (n <= 0) return -1;
    ssize_t written = write(dst_fd, buf, n);
    if (written < 0) return -1;
    return n;
}
```

这段代码有两个简化假设，在生产环境下会出问题。是哪两个？

### C2：cleanup_conn 函数（第 177-190 行）

```c
static void cleanup_conn(int epfd, struct proxy_conn *pc) {
    if (pc->client_fd > 0) {
        epoll_ctl(epfd, EPOLL_CTL_DEL, pc->client_fd, NULL);
        close(pc->client_fd);
        g_conns[pc->client_fd % 1024] = NULL;
    }
    // ... 同样的逻辑处理 backend_fd
    free(pc);
}
```

看着很干净——先 DEL 再 close 再清空数组再 free。但这有一个顺序 bug：如果 client 断开时，数据还没转发完给 backend，会发生什么？

---

## 第三部分：架构理解（画图）

### A1：画数据流向图

用 ASCII 画出以下场景的完整数据流：

```
客户端 nc → 代理:9000 → 后端 echo server:8080

客户端输入 "ping" → 代理转发 → 后端 echo "ping" → 代理转发 → 客户端收到 "ping"
```

标注每一步涉及的 fd、系统调用、以及 epoll_wait 在哪里被唤醒。

---

## 第四部分：实验验证

### E1：strace 验证

启动后端 → 启动代理 → nc 连代理 → 发送数据 → strace 观察代理进程。

```bash
# 终端1: 后端
./epoll-echo

# 终端2: 代理
./tcp-agent

# 终端3: strace 挂代理
ps aux | grep tcp-agent
strace -e trace=connect,accept,read,write,epoll_wait -p <PID> 2>&1

# 终端4: 客户端
nc localhost 9000
> ping
```

找出 `connect` 的返回值——是 0 还是 -1（EINPROGRESS）？为什么你看到的是这个值？

---

## 评分

| 维度 | 题目 | 分值 | 你的回答 |
|------|------|------|---------|
| 设计决策 | D1 backend_ready | 10 | |
| 设计决策 | D2 两个缓冲区 | 10 | |
| 设计决策 | D3 g_conns vs data.ptr | 10 | |
| 正确性 | C1 forward 简化假设 | 15 | |
| 正确性 | C2 cleanup 顺序 bug | 15 | |
| 架构 | A1 数据流向图 | 20 | |
| 实验 | E1 strace 验证 | 20 | |
| **总计** | | **100** | |

---

**不需要写代码。口述 + 截图 + 画图就行。准备好了告诉我，一道一道来。**
