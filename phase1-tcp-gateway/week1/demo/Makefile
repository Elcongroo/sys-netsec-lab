# demo/Makefile — 演示静态链接 vs 动态链接
#
# 关键区别：
#   静态链接: .a 在编译时嵌入可执行文件，运行时无需 .a
#   动态链接: .so 在运行时加载，需要 LD_LIBRARY_PATH 或 rpath
#
# 用法：
#   make static    — 静态链接版本
#   make shared    — 动态链接版本
#   make run-static
#   make run-shared
#   make compare   — 对比文件大小和依赖
#   make clean

CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -std=c11
LIBDIR   = ../libcalc
INCDIR   = ../libcalc

.PHONY: all static shared run-static run-shared compare clean

all: static shared

# ============ 静态链接 ============
static: demo-static

demo-static: main.c $(LIBDIR)/libcalc.a
	@echo "=> 静态链接: $@"
	# -Wl,-Bstatic 强制链接 .a; -Wl,-Bdynamic 恢复默认
	$(CC) $(CFLAGS) -I$(INCDIR) $< -L$(LIBDIR) -Wl,-Bstatic -lcalc -Wl,-Bdynamic -o $@
	@echo ""
	@echo "=== 静态链接信息 ==="
	@echo "文件大小: $$(du -h $@ | cut -f1)"
	@echo "动态库依赖:"
	@ldd $@ 2>&1 | grep -v "not a dynamic" | head -5
	@echo "注: ldd 对纯静态链接只显示静态链接提示"

# ============ 动态链接 ============
shared: demo-shared

demo-shared: main.c $(LIBDIR)/libcalc.so
	@echo "=> 动态链接: $@"
	# $ORIGIN: 运行时从可执行文件所在目录解析相对路径
	# Makefile: $$ → $, 单引号保护不被shell展开
	$(CC) $(CFLAGS) -I$(INCDIR) $< -L$(LIBDIR) -lcalc \
		-Wl,-rpath,'$$ORIGIN/../libcalc' -o $@
	@echo ""
	@echo "=== 动态链接信息 ==="
	@echo "文件大小: $$(du -h $@ | cut -f1)"
	@echo "动态库依赖 (NEEDED):"
	readelf -d $@ 2>/dev/null | grep NEEDED | sed 's/.*\[//;s/\]//' | while read lib; do echo "  - $$lib"; done

run-static: demo-static
	@echo "--- 运行静态链接版本 ---"
	./demo-static

run-shared: demo-shared
	@echo "--- 运行动态链接版本 ---"
	LD_LIBRARY_PATH=$(LIBDIR):$$LD_LIBRARY_PATH ./demo-shared

# ============ 对比 ============
compare: demo-static demo-shared
	@echo "============================================"
	@echo "       静态链接 vs 动态链接 对比"
	@echo "============================================"
	@echo ""
	@echo "文件大小:"
	@printf "  静态: %s\n" "$$(du -h demo-static | cut -f1)"
	@printf "  动态: %s\n" "$$(du -h demo-shared | cut -f1)"
	@echo ""
	@echo "动态库依赖:"
	@echo "  静态版本:"
	@ldd demo-static 2>&1 | sed 's/^/    /'
	@echo "  动态版本 (NEEDED):"
	readelf -d demo-shared 2>/dev/null | grep NEEDED | sed 's/^/    /'
	@echo ""
	@echo "符号来源验证 (nm | grep calc_add):"
	@echo "  静态版本:"
	nm demo-static 2>/dev/null | grep calc_add | sed 's/^/    /' || echo "    (内联或剥离)"
	@echo "  动态版本:"
	nm demo-shared 2>/dev/null | grep calc_add | sed 's/^/    /' || echo "    (动态解析)"

clean:
	rm -f demo-static demo-shared
