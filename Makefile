# Simplified Makefile for innet library

.PHONY: all clean install

# Target names
TARGET := innet
LIBRARY_NAME := $(TARGET)

# Directories
BINDIR := bin
OBJDIR := obj
LIBDIR := lib
SRCDIR := src
INCDIR := include

# Compiler and tools
CXX := g++
AR := ar
LD := g++

# Source files
SRCS := $(SRCDIR)/innet.cpp
OBJS := $(OBJDIR)/innet.o

# Flags
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -fPIC -pthread
CXXFLAGS += -I$(INCDIR)/innet
LDFLAGS := -lpthread -lstdc++

# Targets
all: $(LIBDIR)/lib$(LIBRARY_NAME).so $(LIBDIR)/lib$(LIBRARY_NAME).a

$(LIBDIR)/lib$(LIBRARY_NAME).so: $(OBJS) | $(LIBDIR)
	$(CXX) -shared $(OBJS) $(LDFLAGS) -o $@

$(LIBDIR)/lib$(LIBRARY_NAME).a: $(OBJS) | $(LIBDIR)
	$(AR) rcs $@ $(OBJS)

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Directory creation
$(BINDIR):
	mkdir -p $@

$(OBJDIR):
	mkdir -p $@

$(LIBDIR):
	mkdir -p $@

# Clean
clean:
	rm -rf $(OBJDIR) $(BINDIR) $(LIBDIR)

# Install (optional)
install:
	cp $(LIBDIR)/lib$(LIBRARY_NAME).* /usr/local/lib/
	cp -r $(INCDIR)/* /usr/local/include/