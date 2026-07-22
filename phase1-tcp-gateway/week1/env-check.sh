#!/usr/bin/env bash
#
# env-check.sh — 开发环境自检脚本
# 检查编译链、调试器、分析工具是否就绪
#
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

pass_count=0
fail_count=0
warn_count=0

check_cmd() {
    local name="$1"
    local cmd="$2"
    local version_flag="${3:---version}"

    printf "  %-20s " "$name ..."
    if command -v "$cmd" &>/dev/null; then
        local ver
        ver=$("$cmd" "$version_flag" 2>&1 | head -1 | cut -c1-80)
        printf "${GREEN}✓${NC} %s\n" "$ver"
        ((pass_count++))
    else
        printf "${RED}✗ not found${NC}\n"
        ((fail_count++))
    fi
}

echo "============================================"
echo " sys-netsec-lab 开发环境自检"
echo " 日期: $(date '+%Y-%m-%d %H:%M:%S')"
echo " 主机: $(hostname)"
echo " 内核: $(uname -r)"
echo "============================================"
echo ""

# --- 编译器 ---
echo "[编译器]"
check_cmd "GCC"       gcc       --version
check_cmd "G++"       g++       --version
check_cmd "Clang"     clang     --version
check_cmd "Clang++"   clang++   --version
echo ""

# --- 构建工具 ---
echo "[构建工具]"
check_cmd "Make"      make      --version
check_cmd "CMake"     cmake     --version
check_cmd "pkg-config" pkg-config --version 2>/dev/null || true
echo ""

# --- 调试器 ---
echo "[调试器]"
check_cmd "GDB"       gdb       --version
check_cmd "strace"    strace    --version
check_cmd "ltrace"    ltrace    --version 2>/dev/null || true
echo ""

# --- 二进制工具 ---
echo "[二进制工具]"
check_cmd "objdump"   objdump   --version
check_cmd "readelf"   readelf   --version
check_cmd "nm"        nm        --version
check_cmd "addr2line" addr2line --version 2>/dev/null || true
check_cmd "ldd"       ldd       --version
echo ""

# --- 分析工具 ---
echo "[分析工具]"
check_cmd "Valgrind"  valgrind  --version
# ASan / UBSan check
printf "  %-20s " "ASan/UBSan ..."
if echo "int main(){}" | gcc -x c - -fsanitize=address -fsanitize=undefined -o /tmp/_asan_test 2>/dev/null; then
    printf "${GREEN}✓${NC} supported\n"
    ((pass_count++))
    rm -f /tmp/_asan_test
else
    printf "${RED}✗ not supported${NC}\n"
    ((fail_count++))
fi
# TSan check
printf "  %-20s " "TSan ..."
if echo "int main(){}" | gcc -x c - -fsanitize=thread -o /tmp/_tsan_test 2>/dev/null; then
    printf "${GREEN}✓${NC} supported\n"
    ((pass_count++))
    rm -f /tmp/_tsan_test
else
    printf "${YELLOW}⚠${NC} not available\n"
    ((warn_count++))
fi
echo ""

# --- 网络工具 ---
echo "[网络工具]"
check_cmd "tcpdump"   tcpdump   --version
check_cmd "netcat"    nc        -h 2>&1 | head -1 || true
check_cmd "iptables"  iptables  --version
echo ""

# --- 库 ---
echo "[关键库]"
check_lib() {
    local name="$1"
    local header="$2"
    printf "  %-20s " "$name ..."
    if [ -f "$header" ]; then
        printf "${GREEN}✓${NC} %s\n" "$header"
        ((pass_count++))
    else
        printf "${YELLOW}⚠${NC} not found at $header\n"
        ((warn_count++))
    fi
}
check_lib "OpenSSL"    /usr/include/openssl/ssl.h
check_lib "libpcap"    /usr/include/pcap/pcap.h
echo ""

# --- 结果 ---
echo "============================================"
total=$((pass_count + fail_count + warn_count))
printf "结果: ${GREEN}%d pass${NC}, ${RED}%d fail${NC}, ${YELLOW}%d warn${NC} (共 %d 项)\n" \
    "$pass_count" "$fail_count" "$warn_count" "$total"

if [ "$fail_count" -gt 0 ]; then
    echo ""
    echo "请安装缺失的工具: sudo apt install <package>"
    exit 1
else
    echo "环境就绪 ✓"
    exit 0
fi
