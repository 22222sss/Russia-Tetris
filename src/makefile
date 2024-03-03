CXX = g++
TARGET = test
SRCDIR = ../src
OBJDIR = obj
SOURCES := $(shell find $(SRCDIR) -name '*.cpp')
OBJECTS := $(SOURCES:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)
CXXFLAGS = -Wall -I$(SRCDIR) -levent

$(TARGET): $(OBJECTS)
	$(CXX) -o $@ $^ -levent

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -rf $(OBJDIR)/*.o $(TARGET)
