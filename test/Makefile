# test/Makefile - StreamGuard Test Build System

CC = gcc

# nDPI paths (same logic as src/Makefile)
NDPI_LOCAL = ../nDPI
ifneq ($(wildcard $(NDPI_LOCAL)/src/lib/libndpi.a),)
    NDPI_CFLAGS = -I$(NDPI_LOCAL)/src/include
    NDPI_LIBS = $(NDPI_LOCAL)/src/lib/libndpi.a -lm -lpthread -lgcrypt -lgpg-error
else
    NDPI_CFLAGS = $(shell pkg-config --cflags libndpi)
    NDPI_LIBS = $(shell pkg-config --libs libndpi)
endif

# Criterion flags
CRITERION_LIBS = $(shell pkg-config --libs criterion 2>/dev/null || echo "-lcriterion")

# Common flags
CFLAGS = -Wall -Wextra -g -DTESTING $(NDPI_CFLAGS)
LDFLAGS = $(NDPI_LIBS) -lpcap -lcjson $(CRITERION_LIBS)

# Source files
STREAMGUARD_SRC = ../src/streamguard.c
STREAMGUARD_OBJ = streamguard_test.o

# Unit test files
UNIT_SRCS = $(wildcard unit/*.c)
UNIT_BINS = $(UNIT_SRCS:.c=)

.PHONY: all unit check check-unit integration tap clean

all: unit

# Build streamguard as object file (rename main to avoid conflicts)
$(STREAMGUARD_OBJ): $(STREAMGUARD_SRC)
	$(CC) $(CFLAGS) -Dmain=streamguard_main -c -o $@ $<

# Build each unit test binary
unit/%: unit/%.c $(STREAMGUARD_OBJ)
	$(CC) $(CFLAGS) -I../src -o $@ $< $(STREAMGUARD_OBJ) $(LDFLAGS)

# Build all unit tests
unit: $(UNIT_BINS)

# Run all tests (unit + integration)
check: check-unit integration

# Run unit tests only
check-unit: unit
	@echo "=== Running Unit Tests ==="
	@failed=0; \
	for test in $(UNIT_BINS); do \
		echo "Running $$test..."; \
		./$$test --verbose || failed=1; \
	done; \
	exit $$failed

# Run integration tests only
integration:
	@echo "=== Running Integration Tests ==="
	@cd integration && ./run_tests.sh

# TAP output for CI
tap: unit
	@echo "TAP version 13"
	@for test in $(UNIT_BINS); do \
		./$$test --tap; \
	done

clean:
	rm -f $(STREAMGUARD_OBJ) $(UNIT_BINS)
