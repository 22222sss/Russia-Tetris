# 编译器设置
CXX = g++
CXXFLAGS = -Wall -g -O0 -std=c++17 -pthread -MMD -MP
INCLUDES = -ICommon -IUtility -IEventLoop -ITetrisGame -IUImanage -IConnectionPool
LDFLAGS = -levent -lspdlog -lfmt -pthread   # 添加了 -lfmt

# 源文件
SOURCES = \
    main.cpp \
    Utility/SystemOptimizer.cpp \
    Utility/Utility.cpp \
    EventLoop/EventLoop.cpp \
    TetrisGame/Filedata.cpp \
    TetrisGame/Game.cpp \
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
	@echo "✅ 编译完成: $(TARGET)"

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

.PHONY: all clean rebuild run

# 自动生成的头文件依赖（-MMD -MP）。没有 .d 文件时 -include 会静默跳过，
# 所以首次构建和 clean 之后都不会报错。
-include $(DEPS)