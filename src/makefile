# 编译器设置
CXX := g++
TARGET := tetris_game
SRCDIR := .
OBJDIR := obj

# 获取所有子目录
SUBDIRS := Common Utility User EventLoop TetrisGame Server PlayerInfo UImanage Filedata_manage

# 包含所有子目录的头文件
INCLUDES := $(addprefix -I,$(SUBDIRS))

# 自动查找所有源文件（包括子目录和当前目录）
SOURCES := $(wildcard *.cpp) $(foreach dir,$(SUBDIRS),$(wildcard $(dir)/*.cpp))
OBJECTS := $(patsubst %.cpp,$(OBJDIR)/%.o,$(SOURCES))

# 编译选项
CXXFLAGS := -Wall -g -O0 $(INCLUDES) -std=c++17
LDFLAGS := -levent -lspdlog -pthread -g

# 主规则
$(TARGET): $(OBJECTS)
	$(CXX) -o $@ $^ $(LDFLAGS)

# 编译每个.cpp到.o
$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 清理
.PHONY: clean
clean:
	rm -rf $(OBJDIR) $(TARGET)

# 调试目标
.PHONY: debug
debug: $(TARGET)
	gdb ./$(TARGET)