TARGET = test

CC = gcc
SRCS = test.c juson.c
CFLAGS = -O2 -g -Wall -std=c11 -Wall -I./

OBJS_DIR = build/
OBJS = $(SRCS:.c=.o)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

.PHONY: clean

clean:
	-rm -rf $(TARGET) $(OBJS)
