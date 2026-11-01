# 编译器设置
CXX = g++
# 优化级别：默认 -O2。改它可以做 A/B，但**不同级别测出的性能数字不能混比**
# （历史数据见 docs/HANDOVER.md；-O0 时代的基线是 2026-11-01 之前的全部数据）。
#   make clean && make OPT=-O0     # 复现历史基线
# 压测脚本会把实际级别记进结果目录（make -s print-opt）。
OPT ?= -O2
CXXFLAGS = -Wall -g $(OPT) -std=c++17 -pthread -MMD -MP
INCLUDES = -ICommon -IUtility -IEventLoop -ITetrisGame -IUImanage -IConnectionPool
LDFLAGS = -levent -lspdlog -lfmt -lcrypto -pthread   # -lcrypto: PBKDF2/随机盐（libssl-dev）

# 源文件
SOURCES = \
    main.cpp \
    Utility/SystemOptimizer.cpp \
    Utility/Utility.cpp \
    EventLoop/EventLoop.cpp \
    TetrisGame/Filedata.cpp \
    TetrisGame/Game.cpp \
    TetrisGame/PasswordHash.cpp \
    TetrisGame/PlayerInfo.cpp \
    TetrisGame/Server.cpp \
    TetrisGame/User.cpp \
    UImanage/UImanage.cpp \
    ConnectionPool/ConnectionPool.cpp

# 对象文件
OBJECTS = $(SOURCES:.cpp=.o)
OBJDIR = obj
OBJS = $(addprefix $(OBJDIR)/, $(OBJECTS))
DEPS = $(OBJS:.o=.d)

# 目标
TARGET = tetris_game

# 默认目标
all: $(TARGET)

# 链接目标
$(TARGET): $(OBJS)
	$(CXX) -o $@ $(OBJS) $(LDFLAGS)
	@echo "✅ 编译完成: $(TARGET)（优化级别: $(OPT)）"

# 编译规则
$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@
	@echo "🔨 编译: $<"

# 清理
clean:
	rm -rf $(OBJDIR) $(TARGET)
	@echo "🧹 清理完成"

# 重新编译
rebuild: clean all

# 运行服务器
run: $(TARGET)
	@echo "🚀 启动服务器..."
	./$(TARGET)

# 打印实际生效的优化级别：压测脚本/CI 用它把"口径"写进结果，避免 -O0/-O2 混比
print-opt:
	@echo $(OPT)

.PHONY: all clean rebuild run print-opt

# 自动生成的头文件依赖（-MMD -MP）。没有 .d 文件时 -include 会静默跳过，
# 所以首次构建和 clean 之后都不会报错。
-include $(DEPS)