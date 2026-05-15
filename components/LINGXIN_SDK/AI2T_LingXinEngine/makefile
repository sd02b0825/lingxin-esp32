
# 默认遇到编译错误不中断
MAKEFLAGS += --keep-going

# 编译器
CC ?= gcc
$(info [🔧 MAKE] Active compiler: $(CC))
$(info [🔧 MAKE] Version info: $(shell command -v $(CC) >/dev/null 2>&1 && $(CC) --version 2>/dev/null | head -1 || echo "❌ NOT FOUND"))
$(info )

# 项目根目录
SRC_DIR := ../AI2T_LingXinEngine

# 查找所有 .c 文件（包括子目录）
SOURCES := $(shell find $(SRC_DIR)/src $(SRC_DIR)/tests -name "*.c")

# 生成对应的 .o 文件路径（保持目录结构）
OBJECTS := $(SOURCES:.c=.o)

# 自动收集所有子目录作为头文件搜索路径
INCLUDE_DIRS := $(shell find $(SRC_DIR) -type d)
INCLUDE_FLAGS := $(addprefix -I, $(INCLUDE_DIRS))

# 编译选项：只编译不链接，指定头文件搜索路径，开启所有警告，调试信息
CFLAGS := -c \
		  -Wall \
		  -Werror \
		  -Wextra \
		  -Wno-unused-parameter \
		  -g
#   -Wno-sign-compare \
#   -Wenum-conversion -Werror=enum-conversion \
#   -Wenum-compare -Werror=enum-compare \

# 默认目标：编译所有 .c 文件为 .o
all: $(OBJECTS)

# 通用编译规则：从 .c 生成 .o
%.o: %.c
	@echo "Compiling $< ..."
	@echo "Include flags: $(INCLUDE_FLAGS)"
	$(CC) $(CFLAGS) $(INCLUDE_FLAGS) -o $@ $<

# 清理生成的 .o 文件
clean:
	rm -f $(OBJECTS)

.PHONY: all clean