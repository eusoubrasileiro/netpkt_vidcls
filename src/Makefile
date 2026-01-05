# StreamGuard Makefile

CC = gcc
CFLAGS = -Wall -Wextra -O2 $(shell pkg-config --cflags libndpi)
LDFLAGS = $(shell pkg-config --libs libndpi) -lpcap -lcjson

TARGET = streamguard
SRC = streamguard.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

debug: CFLAGS += -g -DDEBUG
debug: $(TARGET)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/

.PHONY: all debug clean install
