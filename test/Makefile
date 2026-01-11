# test/Makefile - StreamGuard Test Build System

CC = gcc

# nDPI paths (same logic as src/Makefile)
NDPI_LOCAL = ../nDPI
ifneq ($(wildcard $(NDPI_LOCAL)/src/lib/libndpi.a),)
    NDPI_CFLAGS = -I$(NDPI_LOCAL)/src/include -I$(NDPI_LOCAL)/src/lib/third_party/include
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
LOG_SRC = ../src/log.c
LOG_OBJ = log_test.o
FLOW_TABLE_SRC = ../src/flow_table.c
FLOW_TABLE_OBJ = flow_table.o
DETECTION_SRC = ../src/detection.c
DETECTION_OBJ = detection.o
FIREWALL_SRC = ../src/firewall.c
FIREWALL_OBJ = firewall.o
STATE_SRC = ../src/state.c
STATE_OBJ = state.o
CLIENT_QUOTA_SRC = ../src/client_quota.c
CLIENT_QUOTA_OBJ = client_quota.o

# All module objects
MODULE_OBJS = $(FLOW_TABLE_OBJ) $(DETECTION_OBJ) $(FIREWALL_OBJ) $(STATE_OBJ) $(CLIENT_QUOTA_OBJ)

# Unit test files
UNIT_SRCS = $(wildcard unit/*.c)
UNIT_BINS = $(UNIT_SRCS:.c=)

.PHONY: all unit check check-unit integration tap clean

all: unit

# Build streamguard as object file (rename main to avoid conflicts)
$(STREAMGUARD_OBJ): $(STREAMGUARD_SRC)
	$(CC) $(CFLAGS) -Dmain=streamguard_main -c -o $@ $<

# Build log.c as object file
$(LOG_OBJ): $(LOG_SRC)
	$(CC) $(CFLAGS) -c -o $@ $<

# Build flow_table.c as object file
$(FLOW_TABLE_OBJ): $(FLOW_TABLE_SRC)
	$(CC) $(CFLAGS) -I../src -c -o $@ $<

# Build detection.c as object file
$(DETECTION_OBJ): $(DETECTION_SRC)
	$(CC) $(CFLAGS) -I../src -c -o $@ $<

# Build firewall.c as object file
$(FIREWALL_OBJ): $(FIREWALL_SRC)
	$(CC) $(CFLAGS) -I../src -c -o $@ $<

# Build state.c as object file
$(STATE_OBJ): $(STATE_SRC)
	$(CC) $(CFLAGS) -I../src -c -o $@ $<

# Build client_quota.c as object file
$(CLIENT_QUOTA_OBJ): $(CLIENT_QUOTA_SRC)
	$(CC) $(CFLAGS) -I../src -c -o $@ $<

# Build each unit test binary
unit/%: unit/%.c $(STREAMGUARD_OBJ) $(LOG_OBJ) $(MODULE_OBJS)
	$(CC) $(CFLAGS) -I../src -o $@ $< $(STREAMGUARD_OBJ) $(LOG_OBJ) $(MODULE_OBJS) $(LDFLAGS)

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
	rm -f $(STREAMGUARD_OBJ) $(LOG_OBJ) $(MODULE_OBJS) $(UNIT_BINS)
