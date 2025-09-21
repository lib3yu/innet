# Makefile for innet examples
# examples/Makefile

.PHONY: all clean
.PHONY: demo_industry demo_pubsub


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
DEMO_TARGET        := demo_industry
DEMO_PUBSUB_TARGET := demo_pubsub
# Source files
DEMO_SRCS        := $(SRCDIR)/demo_industry.c
DEMO_OBJS        := $(OBJDIR)/demo_industry.o
DEMO_PUBSUB_SRCS := $(SRCDIR)/demo_pubsub.c
DEMO_PUBSUB_OBJS := $(OBJDIR)/demo_pubsub.o

# Flags
CFLAGS  := -O0 -g -Wall -Wextra -pthread
CFLAGS  += -I$(INCDIR)/innet

LDFLAGS := -L$(LIBDIR) -linnet -lpthread

# Targets
all: demo_industry demo_pubsub

demo_industry: $(BINDIR)/$(DEMO_TARGET)

demo_pubsub: $(BINDIR)/$(DEMO_PUBSUB_TARGET)


$(BINDIR)/$(DEMO_TARGET): $(DEMO_OBJS) $(LIBDIR)/libinnet.so | $(BINDIR)
	$(CC) $(DEMO_OBJS) $(LDFLAGS) -o $@

$(BINDIR)/$(DEMO_PUBSUB_TARGET): $(DEMO_PUBSUB_OBJS) $(LIBDIR)/libinnet.so | $(BINDIR)
	$(CC) $(DEMO_PUBSUB_OBJS) $(LDFLAGS) -o $@

# Future demo targets - uncomment and customize as needed
# $(BINDIR)/$(DEMO1_TARGET): $(OBJDIR)/demo1.o $(LIBDIR)/libinnet.so | $(BINDIR)
# 	$(CC) $(OBJDIR)/demo1.o $(LDFLAGS) -o $@


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
	rm -f $(OBJDIR)/demo*.o
	rm -f $(BINDIR)/$(DEMO_TARGET)
	rm -f $(BINDIR)/$(DEMO_PUBSUB_TARGET)
