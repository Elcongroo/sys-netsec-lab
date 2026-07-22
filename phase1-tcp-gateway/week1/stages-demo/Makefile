# stages-demo/Makefile — 编译四阶段逐步演示
#
# 预处理(Preprocess): .c → .i  宏展开、头文件展开、注释删除
# 编译(Compile):      .i → .s  生成汇编代码
# 汇编(Assemble):     .s → .o  生成机器码目标文件
# 链接(Link):         .o → ELF 解析符号，合并段，生成可执行文件
#
# 用法：
#   make all     — 执行全部阶段并展示每步输出
#   make stage1  — 只做预处理
#   make stage2  — 预处理 + 编译
#   make stage3  — 预处理 + 编译 + 汇编
#   make stage4  — 完整链接
#   make clean

CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c11
SRC     = demo.c

.PHONY: all clean stage1 stage2 stage3 stage4

all: stage1 stage2 stage3 stage4
	@echo ""
	@echo "============================================"
	@echo "  四阶段全部完成！运行: ./demo"
	@echo "============================================"
	@./demo

# ============ Stage 1: 预处理 ============
# -E: 只做预处理，输出到 stdout
# -o demo.i: 预处理结果文件
stage1: $(SRC)
	@echo "============================================"
	@echo "  Stage 1: 预处理 (Preprocessing)"
	@echo "  .c → .i : 宏展开 + 头文件展开 + 注释删除"
	@echo "============================================"
	$(CC) -E $(SRC) -o demo.i
	@echo ""
	@echo "  >> 行数变化:"
	@echo "     源文件: $$(wc -l < $(SRC)) 行"
	@echo "     预处理后: $$(wc -l < demo.i) 行"
	@echo ""
	@echo "  >> MSG_PREFIX 宏展开结果 (grep):"
	@grep -n 'printf.*array_sum' demo.i | head -3
	@echo ""
	@echo "  >> 输出文件: demo.i (前30行预览)"
	@head -30 demo.i

# ============ Stage 2: 编译 ============
# -S: 编译成汇编代码，不汇编、不链接
# -fverbose-asm: 添加 C 源码作为汇编注释
stage2: stage1
	@echo ""
	@echo "============================================"
	@echo "  Stage 2: 编译 (Compilation)"
	@echo "  .i → .s : 生成汇编代码"
	@echo "============================================"
	$(CC) -S demo.i -fverbose-asm -o demo.s
	@echo ""
	@echo "  >> 汇编文件: demo.s (关键函数)"
	@grep -A 15 'sum_array:' demo.s | head -20
	@echo "  ..."
	@echo ""
	@echo "  >> 段信息 (grep '^\\.'):"
	@grep '^\.' demo.s | head -10

# ============ Stage 3: 汇编 ============
# -c: 编译 + 汇编，生成目标文件 .o
# objdump -d: 反汇编查看机器码
stage3: stage2
	@echo ""
	@echo "============================================"
	@echo "  Stage 3: 汇编 (Assembly)"
	@echo "  .s → .o : 生成 ELF 目标文件"
	@echo "============================================"
	$(CC) -c demo.s -o demo.o
	@echo ""
	@echo "  >> 目标文件信息:"
	@file demo.o
	@echo ""
	@echo "  >> 段头 (readelf -S):"
	@readelf -S demo.o | head -20
	@echo ""
	@echo "  >> 符号表 (nm):"
	@nm demo.o
	@echo ""
	@echo "  >> 反汇编 sum_array (objdump -d):"
	@objdump -d demo.o | grep -A 20 '<sum_array>:' | head -25

# ============ Stage 4: 链接 ============
# 最终链接生成可执行文件
stage4: stage3
	@echo ""
	@echo "============================================"
	@echo "  Stage 4: 链接 (Linking)"
	@echo "  .o + libc.so → ELF 可执行文件"
	@echo "============================================"
	$(CC) $(CFLAGS) demo.o -o demo
	@echo ""
	@echo "  >> ELF 文件信息:"
	@file demo
	@echo ""
	@echo "  >> 段布局 (readelf -l):"
	@readelf -l demo | grep -E "LOAD|GNU_STACK|INTERP" | head -5
	@echo ""
	@echo "  >> 动态库依赖:"
	@ldd demo
	@echo ""
	@echo "  >> 入口点地址:"
	@readelf -h demo | grep Entry || true

clean:
	rm -f demo.i demo.s demo.o demo
