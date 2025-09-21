# Makefile for innet tests
# tests/Makefile

.PHONY: all clean


# Directories
BINDIR := ../bin
OBJDIR := ../obj
LIBDIR := ../lib
SRCDIR := .
INCDIR := ../include

# Compiler and tools
CC  := gcc
CXX := g++
LD  := g++

# Target names
TEST_TARGET := test-unit
# Source files
TEST_OBJS := $(OBJDIR)/test-unit.o

# Flags
CFLAGS  := -O0 -g -Wall -Wextra -pthread
CFLAGS  += -I$(INCDIR)/innet
CXXFLAGS := $(CFLAGS)

LDFLAGS := -L$(LIBDIR) -linnet -lpthread -lgtest -lgtest_main

# Targets
all: $(BINDIR)/$(TEST_TARGET)


$(BINDIR)/$(TEST_TARGET): $(TEST_OBJS) $(LIBDIR)/libinnet.so | $(BINDIR)
	$(CXX) $(TEST_OBJS) $(LDFLAGS) -o $@

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
	rm -f $(OBJDIR)/test*.o
	rm -f $(BINDIR)/$(TEST_TARGET)
