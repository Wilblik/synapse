CC = gcc
CFLAGS = -g -Wall -Wextra
TARGET = synapse

all: $(TARGET)

synapse: main.c
	$(CC) $(CFLAGS) -o $@ $<

run: all
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean
