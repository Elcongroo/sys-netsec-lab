# W1: 开发环境与编译链

**Date**: 2026-07-27 ~ 08-03
**Theme**: 编译链、构建脚本、环境自检、静态/动态库

## 验收标准

- [ ] 删除 `build/` 后可一键重建
- [ ] 能解释预处理 → 编译 → 汇编 → 链接 四个阶段
- [ ] 能区分静态库(.a)和动态库(.so)的构建与链接方式
- [ ] 环境自检脚本可输出所有工具版本

## 目录结构

```
week1/
├── README.md           # 本文件
├── env-check.sh        # 环境自检脚本
├── libcalc/            # 示例计算库
│   ├── calc.h          #   头文件
│   ├── calc.c          #   实现
│   └── Makefile        #   构建 .a 和 .so
├── demo/               # 链接演示
│   ├── main.c          #   使用 libcalc
│   └── Makefile        #   分别链接 .a / .so
└── stages-demo/        # 编译四阶段演示
    ├── demo.c          #   源文件
    └── Makefile        #   逐步展示每个阶段
```

## 编译四阶段速查

| 阶段 | 命令 | 输入 → 输出 |
|------|------|-------------|
| 预处理 | `gcc -E` | `.c` → `.i` (宏展开、头文件展开) |
| 编译 | `gcc -S` | `.i` → `.s` (汇编代码) |
| 汇编 | `gcc -c` | `.s` → `.o` (目标文件) |
| 链接 | `ld` / `gcc` | `.o` + `.a/.so` → 可执行文件 |

## 关键知识点

1. **静态库**：编译时嵌入，`ar rcs libxxx.a`，链接用 `-lxxx -L.`
2. **动态库**：运行时加载，`gcc -fPIC -shared`，`ldconfig` 或 `LD_LIBRARY_PATH`
3. **符号表**：`nm`、`objdump -T` 查看，链接器按符号解析
4. **rpath vs runpath**：`-Wl,-rpath` 把搜索路径嵌入 ELF
5. **soname**：动态库的版本化标识，`-Wl,-soname,libxxx.so.1`
