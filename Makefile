SRCDIR   := src
OBJDIR   := obj
BINDIR   := bin

SRCS     := $(wildcard $(SRCDIR)/*.c)
OBJS     := $(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(SRCS))
DEPS     := $(OBJS:.o=.d)

CC       := gcc
CFLAGS   := -g -Wall -Wextra -I$(SRCDIR) -MMD -MP

TARGET := $(BINDIR)/synapse

all: $(TARGET)

$(TARGET): $(OBJS) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BINDIR) $(OBJDIR):
	@mkdir -p $@

clean:
	@rm -rf $(BINDIR) $(OBJDIR)

-include $(DEPS)

.PHONY: all clean
