# Makefile — memory-lab 构建系统
#
# Target 说明：
#   make              → 普通编译所有程序（不带检测工具）
#   make test-asan    → 用 ASan 编译并运行，捕捉内存错误
#   make test-valgrind→ 用 Valgrind 运行所有检查
#   make test-ubsan   → 用 UBSan 编译并运行
#   make fixed        → 编译所有修复版本（-DFIX）
#   make clean        → 清理编译产物

CC       = gcc
CFLAGS   = -Wall -Wextra -g -O0
ASAN     = -fsanitize=address
UBSAN    = -fsanitize=undefined

PROGS      = overflow uaf memory-leak uninit double-free
ASAN_PROGS = $(addsuffix -asan, $(PROGS))
UBSAN_PROGS= $(addsuffix -ubsan, $(PROGS))
FIXED_PROGS= $(addsuffix -fixed, $(PROGS))

VALGRIND_FLAGS = --leak-check=full --track-origins=yes --show-leak-kinds=all --error-exitcode=1

# ============ 默认 Target ============
all: $(PROGS)

# ============ 编译规则 ============
%: %.c
	$(CC) $(CFLAGS) -o $@ $<

%-asan: %.c
	$(CC) $(CFLAGS) $(ASAN) -o $@ $<

%-ubsan: %.c
	$(CC) $(CFLAGS) $(UBSAN) -o $@ $<

%-fixed: %.c
	$(CC) $(CFLAGS) -DFIX -o $@ $<

# ============ 测试 Target ============

# ASan：编译 + 逐个运行，从 stderr 中搜索 "ERROR:" 关键字
test-asan: $(ASAN_PROGS)
	@echo ""
	@echo "=========================================="
	@echo "  ASan 测试"
	@echo "=========================================="
	@echo ""
	@ok=0; fail=0; \
	for prog in $(ASAN_PROGS); do \
		printf "[ASan] %-20s ... " "$$prog"; \
		output=$$(./$$prog 2>&1) || true; \
		if echo "$$output" | grep -q "ERROR:"; then \
			echo "FAIL (检测到错误 - 预期)"; fail=$$((fail+1)); \
		else \
			echo "PASS"; ok=$$((ok+1)); \
		fi; \
	done; \
	echo ""; \
	echo "------------------------------------------"; \
	echo "  预期: 全部 FAIL（ASan 应抓到 Bug）"; \
	echo "  结果: $$ok PASS, $$fail FAIL"; \
	echo "------------------------------------------"

# Valgrind：只跑普通版本
test-valgrind: $(PROGS)
	@echo ""
	@echo "=========================================="
	@echo "  Valgrind 测试"
	@echo "=========================================="
	@echo ""
	@ok=0; fail=0; \
	for prog in $(PROGS); do \
		printf "[Valgrind] %-20s ... " "$$prog"; \
		ec=0; output=$$(valgrind $(VALGRIND_FLAGS) ./$$prog 2>&1) || ec=$$?; \
		errors=0; \
		if ! echo "$$output" | grep -q "ERROR SUMMARY: 0"; then errors=1; fi; \
		if [ $$ec -ne 0 ] && [ $$ec -ne 1 ]; then errors=1; fi; \
		if [ $$errors -eq 1 ]; then \
			echo "FAIL (检测到错误 - 预期)"; fail=$$((fail+1)); \
		else \
			echo "PASS (无错误)"; ok=$$((ok+1)); \
		fi; \
	done; \
	echo ""; \
	echo "------------------------------------------"; \
	echo "  预期: 全部 FAIL（Valgrind 应抓到 Bug）"; \
	echo "  结果: $$ok PASS, $$fail FAIL"; \
	echo "------------------------------------------"

# Valgrind 验证修复版本（应全通过）
test-fixed-valgrind: $(FIXED_PROGS)
	@echo ""
	@echo "=========================================="
	@echo "  Valgrind 验证修复版本"
	@echo "=========================================="
	@echo ""
	@ok=0; fail=0; \
	for prog in $(FIXED_PROGS); do \
		printf "[Valgrind] %-20s ... " "$$prog"; \
		output=$$(valgrind $(VALGRIND_FLAGS) ./$$prog 2>&1) || true; \
		if echo "$$output" | grep -q "ERROR SUMMARY: 0"; then \
			echo "PASS"; ok=$$((ok+1)); \
		else \
			echo "FAIL (不应有错误!)"; fail=$$((fail+1)); \
		fi; \
	done; \
	echo ""; \
	echo "------------------------------------------"; \
	echo "  预期: 全部 PASS（修复后无错误）"; \
	echo "  结果: $$ok PASS, $$fail FAIL"; \
	echo "------------------------------------------"

# UBSan：检测未定义行为
test-ubsan: $(UBSAN_PROGS)
	@echo ""
	@echo "=========================================="
	@echo "  UBSan 测试"
	@echo "=========================================="
	@echo ""
	@ok=0; fail=0; \
	for prog in $(UBSAN_PROGS); do \
		printf "[UBSan] %-20s ... " "$$prog"; \
		output=$$(./$$prog 2>&1) || true; \
		if echo "$$output" | grep -q "runtime error"; then \
			echo "FAIL (检测到未定义行为)"; fail=$$((fail+1)); \
		else \
			echo "PASS"; ok=$$((ok+1)); \
		fi; \
	done; \
	echo ""; \
	echo "  $$ok PASS, $$fail FAIL"; \

# ============ 便捷 Target ============
fixed: $(FIXED_PROGS)

test-all: test-asan test-valgrind test-ubsan
	@echo ""
	@echo "========== 全部测试完成 =========="

# ============ 清理 ============
clean:
	rm -f $(PROGS) $(ASAN_PROGS) $(UBSAN_PROGS) $(FIXED_PROGS)

.PHONY: all test-asan test-valgrind test-ubsan test-fixed-valgrind test-all fixed clean
