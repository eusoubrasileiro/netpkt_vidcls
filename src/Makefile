# StreamGuard Makefile

CC = gcc

# Use local nDPI build (../nDPI) if available, otherwise system pkg-config
NDPI_LOCAL = ../nDPI
ifneq ($(wildcard $(NDPI_LOCAL)/src/lib/libndpi.a),)
    NDPI_CFLAGS = -I$(NDPI_LOCAL)/src/include
    NDPI_LIBS = $(NDPI_LOCAL)/src/lib/libndpi.a -lm -lpthread -lgcrypt -lgpg-error
else
    NDPI_CFLAGS = $(shell pkg-config --cflags libndpi)
    NDPI_LIBS = $(shell pkg-config --libs libndpi)
endif

CFLAGS = -Wall -Wextra -O2 $(NDPI_CFLAGS)
LDFLAGS = $(NDPI_LIBS) -lpcap -lcjson

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

# Run all tests (unit + integration)
test: $(TARGET)
	$(MAKE) -C ../test check

# Run unit tests only
test-unit: $(TARGET)
	$(MAKE) -C ../test check-unit

# Run integration tests only
test-integration: $(TARGET)
	$(MAKE) -C ../test integration

# TAP output for CI
test-tap: $(TARGET)
	$(MAKE) -C ../test tap

.PHONY: all debug clean install test test-unit test-integration test-tap
