CC = gcc
CFLAGS = -g -Wall -Wextra
TARGETS = synapse

all: $(TARGETS)

synapse: main.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGETS)

.PHONY: all clean
