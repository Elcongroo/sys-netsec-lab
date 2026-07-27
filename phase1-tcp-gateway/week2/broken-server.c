/**
 * broken-server.c — 隐藏了 8 个内存 Bug 的"服务器"程序（全部已修复）
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
    char*  recv_buf;    /* 接收缓冲区 */
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
 * BUG #1: session_create 没有初始化所有字段 — 已修复
 * 问题：malloc 后的内存未全部初始化，指针字段是垃圾值
 * Valgrind 会报：Conditional jump depends on uninitialised value
 * 修复：显式将所有指针初始化为 NULL，size_t 初始化为 0
 */
session_t* session_create(int id) {
    session_t* s = (session_t*)malloc(sizeof(session_t));
    if (s == NULL) {
        return NULL;
    }
    s->id = id;
    s->state = STATE_NEW;
    s->recv_buf    = NULL;
    s->packet_data = NULL;
    s->data_len    = 0;
    return s;
}

/*
 * BUG #2: session_destroy 泄漏内存 — 已修复
 * 问题：free 了 session 结构体，但没有先 free 它内部的堆指针
 * Valgrind 会报：definitely lost（recv_buf 和 packet_data 变成孤儿内存）
 * 修复：由内向外释放 —— 先 free 成员指针，再 free 结构体，最后置 NULL
 */
void session_destroy(int conn_id) {
    if (g_connections[conn_id] != NULL) {
        session_t* s = g_connections[conn_id];
        free(s->recv_buf);
        free(s->packet_data);
        free(s);
        g_connections[conn_id] = NULL;
    }
}

/*
 * BUG #3: 缓冲区溢出 — 已修复
 * 问题：原代码用 strcpy 不检查目标大小，且 recv_buf 从未分配
 * ASan 会报：heap-buffer-overflow；Valgrind 报 Invalid write to NULL
 * 修复：
 *   1. 首次接收时 malloc(BUF_SIZE)，并检查返回值
 *   2. 用 strncpy 限制复制长度
 *   3. 强制以 '\0' 结尾
 *   4. 每次接收都覆盖写入，而不只是第一次
 */
int session_recv(session_t* s, const char* data) {
    if (s->recv_buf == NULL) {
        s->recv_buf = (char*)malloc(BUF_SIZE);
        if (s->recv_buf == NULL) {
            return -1;
        }
    }
    strncpy(s->recv_buf, data, BUF_SIZE - 1);
    s->recv_buf[BUF_SIZE - 1] = '\0';
    return 0;
}

/*
 * BUG #4: Use-After-Free — 已修复
 * 问题：session_close 调用 session_destroy 释放了内存，
 *       但没有把 g_connections[conn_id] 置为 NULL，
 *       后续代码检查 if(g_connections[0]!=NULL) 仍然通过，读取野指针
 * Valgrind 会报：Invalid read of size 4 ... Address ... is ... inside a block ... free'd
 * 修复：session_destroy 内部统一置 NULL（见 BUG #2 修复）
 */
void session_close(int conn_id) {
    session_t* s = g_connections[conn_id];
    if (s != NULL) {
        s->state = STATE_CLOSED;
        session_destroy(conn_id);   /* 内部已置 g_connections[conn_id] = NULL */
        g_conn_count--;
    }
}

/*
 * BUG #5: 未初始化内存读取 — 已修复
 * 问题：malloc 分配了 128 字节但从未填充，读取时拿到的是垃圾值
 * Valgrind 会报：Use of uninitialised value / Conditional jump depends on uninitialised value
 * 修复：memset 清零，这样读出来的值至少是确定的（全 0x00）
 */
char* session_get_data(session_t* s) {
    if (s->packet_data == NULL) {
        s->packet_data = (char*)malloc(128);
        if (s->packet_data != NULL) {
            memset(s->packet_data, 0, 128);
        }
    }
    return s->packet_data;
}

/*
 * BUG #6: 双重释放（Double Free）— 已修复
 * 问题：header[0]=='X' 时 free(header) 但没有 return NULL，
 *       调用者 process_packet 拿到已释放的指针后又 free 了一次
 * Valgrind 会报：Invalid free() ... Address ... is ... inside a block ... free'd
 * 修复：
 *   1. free 后立即 return NULL，让调用者知道"没有可用的 header"
 *   2. 顺便给 g_corrupted_packets 计数（语义正确）
 *   3. 增加 malloc 返回值检查
 */
char* parse_header(const char* raw, size_t len) {
    if (len < HEADER_SIZE) {
        return NULL;
    }
    char* header = (char*)malloc(HEADER_SIZE + 1);
    if (header == NULL) {
        return NULL;
    }
    memcpy(header, raw, HEADER_SIZE);
    header[HEADER_SIZE] = '\0';

    /* 检查"损坏"标志：首字节为 'X' 视为损坏 */
    if (header[0] == 'X') {
        free(header);
        g_corrupted_packets++;
        return NULL;   /* 关键：释放后必须返回 NULL */
    }

    return header;
}

/*
 * BUG #7: 内存泄漏（提前返回路径）— 已修复
 * 问题：多个错误路径上分配了 decoded/header 但没有释放就直接 return -1
 * Valgrind 会报：definitely lost（短包和损坏包路径泄漏 decoded）
 * 修复：用 goto cleanup 模式，所有路径统一在 cleanup 标签处释放
 *
 * 这是 C 语言中非常经典的模式：
 *   - 资源分配顺序：先 decoded，再 header，再 payload
 *   - 释放顺序：反方向，先 payload，再 header，再 decoded
 *   - 每个指针初始化为 NULL，cleanup 处 free(NULL) 是安全的
 */
int process_packet(const char* raw_data, size_t len) {
    char* decoded = NULL;
    char* header  = NULL;
    char* payload = NULL;

    decoded = (char*)malloc(len);
    if (decoded == NULL) {
        return -1;
    }
    memcpy(decoded, raw_data, len);

    header = parse_header(decoded, len);
    if (header == NULL) {
        /* 短包或损坏包，不处理但必须清理已分配的资源 */
        goto cleanup;
    }

    /* len 必须大于 HEADER_SIZE 才有 payload */
    if (len <= HEADER_SIZE) {
        goto cleanup;
    }

    payload = (char*)malloc(len - HEADER_SIZE);
    if (payload == NULL) {
        goto cleanup;
    }
    memcpy(payload, decoded + HEADER_SIZE, len - HEADER_SIZE);

    g_total_packets++;

cleanup:
    free(payload);
    free(header);
    free(decoded);
    return 0;
}

/*
 * BUG #8: 空指针解引用 — 已修复
 * 问题：没有检查 malloc 的返回值，内存不足时 strcpy 会写到 NULL
 * 修复：malloc 后立即检查是否为 NULL
 */
char* duplicate_string(const char* src) {
    size_t len = strlen(src);
    char* dst = (char*)malloc(len + 1);
    if (dst == NULL) {
        return NULL;
    }
    strcpy(dst, src);
    return dst;
}

/* ============ 模拟主程序 ============ */

int main(void) {
    printf("=== Broken Server — Memory Bug Demo (全部已修复) ===\n\n");

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
    process_packet("SHORT", 5);                        /* 短包，无 payload */
    process_packet("XDAMAGED_PACKET_DATA_XXXXX", 24);  /* 损坏包，触发 BUG #6 路径 */

    /* 模拟：获取数据 */
    printf("[TEST 4] 获取会话数据...\n");
    char* data = session_get_data(g_connections[0]);
    if (data) {
        printf("  数据前 4 字节: %02x %02x %02x %02x\n",
               (unsigned char)data[0], (unsigned char)data[1],
               (unsigned char)data[2], (unsigned char)data[3]);
    }

    /* 模拟：字符串处理 */
    printf("[TEST 5] 字符串处理...\n");
    char* dup = duplicate_string("hello");
    if (dup) {
        printf("  复制结果: %s\n", dup);
        free(dup);   /* 修复：用完后释放 */
    }

    /* 模拟：关闭连接 */
    printf("[TEST 6] 关闭连接...\n");
    session_close(0);
    /* session_destroy 已置 NULL，下面这行不会再执行 */
    if (g_connections[0] != NULL) {
        printf("  连接 0 状态: %d\n", g_connections[0]->state);
    } else {
        printf("  连接 0 已关闭，指针已置 NULL\n");
    }

    /* 清理剩余连接 */
    for (int i = 1; i < MAX_CONNECTIONS; i++) {
        if (g_connections[i] != NULL) {
            session_destroy(i);
        }
    }

    /* 模拟：损坏计数 */
    printf("\n=== 统计 ===\n");
    printf("  总包数: %d\n", g_total_packets);
    printf("  丢弃: %d\n", g_dropped_packets);
    printf("  损坏: %d\n", g_corrupted_packets);
    printf("  连接数: %d\n", g_conn_count);

    printf("\n=== 程序结束（全部 Bug 已修复） ===\n");
    return 0;
}
