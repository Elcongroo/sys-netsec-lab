#!/bin/bash
# run-intermittent.sh
# 循环运行 intermittent-bug 100 次，统计崩溃次数
#
# 用法：
#   ./run-intermittent.sh            # 普通版本，100 次
#   ./run-intermittent.sh 500        # 自定义次数
#   ./run-intermittent.sh --asan     # ASan 版本

COUNT=100
BIN="./intermittent-bug"
USE_ASAN=false

for arg in "$@"; do
    case "$arg" in
        --asan) USE_ASAN=true; BIN="./intermittent-bug-asan" ;;
        [1-9]*) COUNT="$arg" ;;
    esac
done

if $USE_ASAN; then
    echo "=== 使用 AddressSanitizer 版本 ==="
else
    echo "=== 使用普通版本（无内存检测） ==="
fi

if [ ! -x "$BIN" ]; then
    echo "错误：$BIN 不存在，请先编译"
    echo "  gcc -Wall -g -O0 -o intermittent-bug intermittent-bug.c"
    echo "  gcc -fsanitize=address -g -O0 -o intermittent-bug-asan intermittent-bug.c"
    exit 1
fi

echo "运行 $COUNT 次..."
echo

crash=0
survive=0

for ((i = 1; i <= COUNT; i++)); do
    if "$BIN" > /dev/null 2>&1; then
        ((survive++))
    else
        ((crash++))
    fi
    # 每 25 次打印一次进度
    if (( i % 25 == 0 )); then
        printf "  [%3d/%d] 崩溃: %d, 存活: %d\n" "$i" "$COUNT" "$crash" "$survive"
    fi
done

echo
echo "==========================="
echo "       统计结果"
echo "==========================="
printf "  总运行次数 : %d\n" "$COUNT"
printf "  崩溃次数   : %d  (%.1f%%)\n" "$crash"   "$(echo "scale=1; $crash*100/$COUNT" | bc 2>/dev/null || echo "?")"
printf "  存活次数   : %d  (%.1f%%)\n" "$survive" "$(echo "scale=1; $survive*100/$COUNT" | bc 2>/dev/null || echo "?")"

echo
if $USE_ASAN; then
    echo "  ASan 结论："
    if [ "$crash" -eq "$COUNT" ]; then
        echo "  100% 检测到溢出！每一个越界写入都被 ASan 拦截。"
        echo "  对比普通版本（0 次崩溃），ASan 把'隐形'bug 变成了'可见'bug。"
    fi
else
    echo "  关键发现："
    echo "    程序运行了 $COUNT 次，看起来全都'正常'——"
    echo "    但每一次运行，堆都被悄悄破坏了。"
    echo "    这种 bug 在生产环境中可能表现为："
    echo "      - 运行 3 天后突然 segfault"
    echo "      - 改了无关代码后突然崩溃（堆布局变了）"
    echo "      - 在客户环境崩溃，在自己机器上永远不崩"
fi
