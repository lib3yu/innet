# Makefile for innet library

.PHONY: all clean install demo test

# Target names
TARGET := innet
LIBRARY_NAME := $(TARGET)

DEMO_TARGETS := demo_industry demo_pubsub

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
CXXFLAGS := -std=c++11 -O0 -g -Wall -Wextra -fPIC -pthread
CXXFLAGS += -I$(INCDIR)/innet
LDFLAGS := -lpthread

# Targets
all: $(LIBDIR)/lib$(LIBRARY_NAME).so $(LIBDIR)/lib$(LIBRARY_NAME).a

$(LIBDIR)/lib$(LIBRARY_NAME).so: $(OBJS) | $(LIBDIR)
	$(CXX) -shared $(OBJS) $(LDFLAGS) -o $@

$(LIBDIR)/lib$(LIBRARY_NAME).a: $(OBJS) | $(LIBDIR)
	$(AR) rcs $@ $(OBJS)

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(DEMO_TARGETS): all 
	$(MAKE) -C examples $@

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

demo: $(DEMO_TARGETS)
	@echo "\e[32m" 
	@echo "### Successfully built all demo programs: $^"
	@echo "### Run each demo with:"
	@for program in $(DEMO_TARGETS); do \
		echo "    LD_LIBRARY_PATH=lib/ ./$(BINDIR)/$$program"; \
	done
	@echo "\e[0m"

test: all
	$(MAKE) -C tests
	@echo "\e[32m" 
	@echo "### Successfully built test programs: $^"
	@echo "### Run test: LD_LIBRARY_PATH=lib/ ./bin/test-unit" 
	@echo "\e[0m"
