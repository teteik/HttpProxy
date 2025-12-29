CC = gcc
CFLAGS = -Wall -Wextra -g -std=c99 -pthread -D_POSIX_C_SOURCE=200809L
SRCS = main.c client_handler.c http_parser.c origin.c cache.c
TARGET = proxy

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm - f $(TARGET)

.PHONY: clean