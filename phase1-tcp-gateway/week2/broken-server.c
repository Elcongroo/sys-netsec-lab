/**
 * broken-server.c — 隐藏了 8 个内存 Bug 的"服务器"程序
 *
 * 场景：一个简化的连接处理器，接收数据包并进行处理。
 * 你的任务：
 *   1. 用 Valgrind 跑这个程序
 *   2. 看懂每一条错误报告（错误类型、发生在哪一行、分配在哪一行）
 *   3. 分类所有泄漏（definitely lost / possibly lost / still reachable）
 *   4. 逐一修复所有 Bug
 *   5. 修复后用 Valgrind 验证：没有错误、没有泄漏
 *
 * 编译：
 *   gcc -Wall -Wextra -g -O1 -o broken-server broken-server.c
 *
 * 用 Valgrind 跑：
 *   valgrind --leak-check=full --track-origins=yes --show-leak-kinds=all ./broken-server
 *
 * 用 ASan 跑：
 *   gcc -fsanitize=address -g -O1 -o broken-server-asan broken-server.c
 *   ./broken-server-asan
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_CONNECTIONS 100
#define BUF_SIZE 32
#define HEADER_SIZE 5

/* 会话状态 */
typedef enum {
    STATE_NEW,
    STATE_CONNECTED,
    STATE_CLOSED
} session_state_t;

/* 连接会话 */
typedef struct {
    int    id;
    char*  recv_buf;   /* 接收缓冲区 */
    char*  packet_data; /* 已解析的数据 */
    size_t data_len;
    session_state_t state;
} session_t;

/* 全局连接表（静态数组模拟） */
static session_t* g_connections[MAX_CONNECTIONS];
static int        g_conn_count = 0;

/* 统计计数器 */
static int g_total_packets     = 0;
static int g_dropped_packets   = 0;
static int g_corrupted_packets = 0;

/* ============ 辅助函数 ============ */

/*
 * BUG #1: session_create 没有初始化所有字段
 * 问题：malloc 后的内存未全部初始化
 * Valgrind 会报：Conditional jump depends on uninitialised value
 */
session_t* session_create(int id) {
    session_t* s = (session_t*)malloc(sizeof(session_t));
    s->id = id;
    s->state = STATE_NEW;
    /* 忘记分配 buffer */
    return s;
}

/*
 * BUG #2: session_destroy 泄漏内存
 * 问题：free 了 session，但没有 free 它的成员
 * Valgrind 会报：definitely lost
 */
void session_destroy(int conn_id) {
    if (g_connections[conn_id] != NULL) {
        free(g_connections[conn_id]);
        /* 忘记 free 成员变量 */
    }
}

/*
 * BUG #3: 缓冲区溢出
 * 问题：strcpy 不检查目标缓冲区大小
 * ASan 会报：heap-buffer-overflow
 */
int session_recv(session_t* s, const char* data) {
    if (s->recv_buf == NULL) {
        s->recv_buf = (char*)malloc(BUF_SIZE);
    }
    strcpy(s->recv_buf, data);  /* data 可能超过 BUF_SIZE */
    return 0;
}

/*
 * BUG #4: Use-After-Free
 * 问题：外部调用 session_close 后，conn_count 减了但指针没清，
 *       后续代码可能继续使用已释放的 session
 */
void session_close(int conn_id) {
    session_t* s = g_connections[conn_id];
    if (s != NULL) {
        s->state = STATE_CLOSED;
        session_destroy(conn_id);
        g_conn_count--;
        /* 忘记 g_connections[conn_id] = NULL */
    }
}

/*
 * BUG #5: 未初始化内存读取
 * 问题：packet_data 从未被赋值就被读取
 */
char* session_get_data(session_t* s) {
    if (s->packet_data == NULL) {
        s->packet_data = (char*)malloc(128);
        /* 忘记填充数据 */
    }
    return s->packet_data;
}

/*
 * BUG #6: 双重释放
 * 问题：某些错误路径释放了 buf，但调用者不知道，又释放了一次
 */
char* parse_header(const char* raw, size_t len) {
    if (len < HEADER_SIZE) {
        return NULL;
    }
    char* header = (char*)malloc(HEADER_SIZE + 1);
    memcpy(header, raw, HEADER_SIZE);
    header[HEADER_SIZE] = '\0';

    /* 故意模拟：检查"损坏"标志 */
    if (header[0] == 'X') {
        free(header);
        /* 注意：有些错误路径释放了但没有 return NULL */
        /* 调用者不知道，继续使用这个指针 */
    }

    return header;
}

/*
 * BUG #7: 内存泄漏（提前返回路径）
 * 问题：错误路径上分配了资源但没有释放
 */
int process_packet(const char* raw_data, size_t len) {
    char* decoded = (char*)malloc(len);
    char* header  = NULL;
    char* payload = NULL;

    if (decoded == NULL) {
        return -1;
    }

    /* 解码数据 */
    memcpy(decoded, raw_data, len);

    /* 解析头部 */
    header = parse_header(decoded, len);
    if (header == NULL) {
        return -1;  /* ← 泄漏 decoded */
    }

    /* 提取 payload */
    payload = (char*)malloc(len - HEADER_SIZE);
    if (payload == NULL) {
        /* ← 泄漏 decoded 和 header */
        return -1;
    }
    memcpy(payload, decoded + HEADER_SIZE, len - HEADER_SIZE);

    /* 正常处理... */
    g_total_packets++;

    /* 清理 */
    free(header);
    free(payload);
    free(decoded);
    return 0;
}

/*
 * BUG #8: 空指针解引用
 * 问题：没有检查 malloc 的返回值
 */
char* duplicate_string(const char* src) {
    size_t len = strlen(src);
    char* dst = (char*)malloc(len + 1);
    strcpy(dst, src);
    return dst;
}

/* ============ 模拟主程序 ============ */

int main(void) {
    printf("=== Broken Server — Memory Bug Demo ===\n\n");

    /* 模拟：创建几个连接 */
    printf("[TEST 1] 创建连接...\n");
    for (int i = 0; i < 5; i++) {
        g_connections[i] = session_create(i);
        if (g_connections[i]) {
            g_conn_count++;
        }
    }

    /* 模拟：接收数据 */
    printf("[TEST 2] 接收数据...\n");
    const char* test_data[] = {
        "GET /index.html HTTP/1.1",
        "HELLO",
        "this_is_a_very_long_string_that_exceeds_buffer_size!!!!!!!!!!!!",
        NULL,
        "DATA"
    };
    for (int i = 0; test_data[i] != NULL && i < 5; i++) {
        if (g_connections[i] && test_data[i]) {
            session_recv(g_connections[i], test_data[i]);
        }
    }

    /* 模拟：处理数据包 */
    printf("[TEST 3] 处理数据包...\n");
    process_packet("NORMAL_PACKET_DATA_12345", 23);  /* 正常包 */
    process_packet("SHORT", 5);                        /* 短包，触发错误路径 */
    process_packet("XDAMAGED_PACKET_DATA_XXXXX", 24);  /* 损坏包，触发 UAF */

    /* 模拟：获取数据 */
    printf("[TEST 4] 获取会话数据...\n");
    char* data = session_get_data(g_connections[0]);
    printf("  数据前 4 字节: %02x %02x %02x %02x\n",
           (unsigned char)data[0], (unsigned char)data[1],
           (unsigned char)data[2], (unsigned char)data[3]);
    /* 注意：这里打印的值是未初始化的！ */

    /* 模拟：字符串处理 */
    printf("[TEST 5] 字符串处理...\n");
    char* dup = duplicate_string("hello");
    printf("  复制结果: %s\n", dup);
    /* 忘记 free(dup) */

    /* 模拟：关闭连接 */
    printf("[TEST 6] 关闭连接...\n");
    session_close(0);  /* 关闭后 g_connections[0] 还在 */
    /* 下面这行再次使用已释放的 session — UAF */
    if (g_connections[0] != NULL) {
        printf("  连接 0 状态: %d (已释放的野指针!)\n",
               g_connections[0]->state);
    }

    /* 模拟：损坏计数 */
    printf("\n=== 统计 ===\n");
    printf("  总包数: %d\n", g_total_packets);
    printf("  丢弃: %d\n", g_dropped_packets);
    printf("  损坏: %d\n", g_corrupted_packets);
    printf("  连接数: %d\n", g_conn_count);

    printf("\n=== 程序结束（有 bug 未修复） ===\n");
    return 0;
}
