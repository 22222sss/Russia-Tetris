# 编译器设置
CXX := g++
TARGET := tetris_game  # 避免与main目录冲突
SRCDIR := .
OBJDIR := obj
INCLUDES := -ICommon -IUtility -IUser -IEventLoop -ITetrisGame -IServer -IPlayerInfo -IUImanage -IFiledata_manage

# 自动查找所有源文件
SOURCES := $(shell find $(SRCDIR) -name '*.cpp' ! -path './$(OBJDIR)/*')
OBJECTS := $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SOURCES))

# 编译选项
CXXFLAGS := -Wall -g $(INCLUDES)
LDFLAGS := -levent

# 主规则
$(TARGET): $(OBJECTS)
	$(CXX) -o $@ $^ $(LDFLAGS)

# 编译每个.cpp到.o
$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 清理
.PHONY: clean
clean:
	rm -rf $(OBJDIR) $(TARGET)