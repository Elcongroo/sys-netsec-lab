# libcalc Makefile — 构建静态库(.a)和动态库(.so)
#
# 关键概念：
#   -fPIC   : Position Independent Code，动态库必须
#   ar rcs  : 创建静态库归档（r=插入, c=创建, s=索引）
#   soname  : 动态库的版本化逻辑名称，运行时按此查找
#
# 用法：
#   make          — 同时构建 .a 和 .so
#   make static   — 只构建静态库
#   make shared   — 只构建动态库
#   make clean    — 清理

CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -std=c11
AR       = ar
ARFLAGS  = rcs

# 动态库版本
LIB_SO_VERSION = 1
LIB_NAME       = calc

# 源文件
SRC = calc.c
OBJ = calc.o

.PHONY: all static shared clean

all: static shared

# ============ 静态库 libcalc.a ============
static: lib$(LIB_NAME).a

lib$(LIB_NAME).a: $(OBJ)
	@echo "=> 创建静态库: $@"
	$(AR) $(ARFLAGS) $@ $^
	@echo "   符号表:"
	@nm $@ | grep " T " || true

$(OBJ): $(SRC) calc.h
	$(CC) $(CFLAGS) -c $< -o $@

# ============ 动态库 libcalc.so ============
shared: lib$(LIB_NAME).so

# -fPIC: 位置无关代码，动态库必需
# 版本化命名：libcalc.so.1 (实际文件) ← libcalc.so (链接时用)
lib$(LIB_NAME).so: calc_pic.o
	@echo "=> 创建动态库: lib$(LIB_NAME).so.$(LIB_SO_VERSION)"
	$(CC) -shared -Wl,-soname,lib$(LIB_NAME).so.$(LIB_SO_VERSION) \
		-o lib$(LIB_NAME).so.$(LIB_SO_VERSION) $^
	ln -sf lib$(LIB_NAME).so.$(LIB_SO_VERSION) lib$(LIB_NAME).so
	@echo "   符号链接: lib$(LIB_NAME).so -> lib$(LIB_NAME).so.$(LIB_SO_VERSION)"
	@echo "   导出符号:"
	@objdump -T lib$(LIB_NAME).so.$(LIB_SO_VERSION) | grep -E "calc_" || true

calc_pic.o: $(SRC) calc.h
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

# ============ 清理 ============
clean:
	rm -f *.o *.a *.so *.so.*

# ============ 分析辅助 ============
# 查看静态库内容
inspect-static: lib$(LIB_NAME).a
	@echo "=== 静态库成员 ==="
	$(AR) t $^
	@echo ""
	@echo "=== 导出符号 (T=代码, D=数据) ==="
	nm $^
	@echo ""
	@echo "=== 符号解析 ==="
	objdump -d $^ | head -80

# 查看动态库内容
inspect-shared: lib$(LIB_NAME).so
	@echo "=== 动态符号表 ==="
	objdump -T $^
	@echo ""
	@echo "=== ELF 头信息 ==="
	readelf -d $^ | head -30
