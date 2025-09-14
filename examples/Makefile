# Makefile for innet examples

.PHONY: all demo demo1 demo2 demo3 clean

# Target names
DEMO_TARGET := demo
DEMO1_TARGET := demo1
DEMO2_TARGET := demo2
DEMO3_TARGET := demo3

# Directories
BINDIR := ../bin
OBJDIR := ../obj
LIBDIR := ../lib
SRCDIR := .
INCDIR := ../include

# Compiler and tools
CC := gcc
CXX := g++
LD := g++

# Source files
DEMO_SRCS := $(SRCDIR)/demo.c
DEMO_OBJS := $(OBJDIR)/demo.o

# Flags
CFLAGS := -std=c11 -O2 -Wall -Wextra -pthread
CFLAGS += -I$(INCDIR)/innet
LDFLAGS := -L$(LIBDIR) -linnet -lpthread

# Targets
all: demo

demo: $(BINDIR)/$(DEMO_TARGET)

demo1: $(BINDIR)/$(DEMO1_TARGET)

demo2: $(BINDIR)/$(DEMO2_TARGET)

demo3: $(BINDIR)/$(DEMO3_TARGET)

$(BINDIR)/$(DEMO_TARGET): $(DEMO_OBJS) $(LIBDIR)/libinnet.so | $(BINDIR)
	$(CC) $(DEMO_OBJS) $(LDFLAGS) -o $@

# Future demo targets - uncomment and customize as needed
# $(BINDIR)/$(DEMO1_TARGET): $(OBJDIR)/demo1.o $(LIBDIR)/libinnet.so | $(BINDIR)
# 	$(CC) $(OBJDIR)/demo1.o $(LDFLAGS) -o $@

# $(BINDIR)/$(DEMO2_TARGET): $(OBJDIR)/demo2.o $(LIBDIR)/libinnet.so | $(BINDIR)
# 	$(CXX) $(OBJDIR)/demo2.o $(LDFLAGS) -o $@

# $(BINDIR)/$(DEMO3_TARGET): $(OBJDIR)/demo3.o $(LIBDIR)/libinnet.so | $(BINDIR)
# 	$(CC) $(OBJDIR)/demo3.o $(LDFLAGS) -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Directory creation
$(BINDIR):
	mkdir -p $@

$(OBJDIR):
	mkdir -p $@

# Clean
clean:
	rm -f $(OBJDIR)/demo*.o $(BINDIR)/$(DEMO_TARGET) $(BINDIR)/$(DEMO1_TARGET) $(BINDIR)/$(DEMO2_TARGET) $(BINDIR)/$(DEMO3_TARGET)

# Help
help:
	@echo "Available targets:"
	@echo "  make demo     - Build the main demo program"
	@echo "  make demo1    - Build demo1 (when implemented)"
	@echo "  make demo2    - Build demo2 (when implemented)"
	@echo "  make demo3    - Build demo3 (when implemented)"
	@echo "  make all      - Build all available demos"
	@echo "  make clean    - Clean built objects and binaries"