CC      = cc
VERSION = $(shell git describe --tags --always 2>/dev/null || echo "0.0.0")
CFLAGS  = -Wall -Wextra -Werror -pedantic -std=c11 -D_DEFAULT_SOURCE \
          -Iinclude -Ivendor -O2 -g -DRELAY_VERSION='"$(VERSION)"'

SRC     = $(wildcard src/*.c)
OBJ     = $(SRC:src/%.c=build/%.o)

# Test files: all test_*.c files (not test_runner.c — that's the main)
TEST_SRC   = $(filter-out tests/test_runner.c, $(wildcard tests/test_*.c))
VENDOR_SRC = vendor/cJSON/cJSON.c vendor/Unity/unity.c

# Source files to include in test build (exclude main.c only)
TESTABLE_SRC = $(filter-out src/main.c, $(wildcard src/*.c))

# ── Platform detection ──────────────────────────────────────────────── #

UNAME := $(shell uname)

# GCC-only: suppress false-positive truncation warnings on snprintf
# (Clang doesn't emit these and doesn't recognise the flags)
# Linux also needs -lutil for openpty()
ifneq ($(UNAME), Darwin)
CFLAGS += -Wno-format-truncation -Wno-stringop-truncation
LDLIBS_EXTRA = -lutil
else
LDLIBS_EXTRA =
endif

# ── Production build ────────────────────────────────────────────────── #

relay: build $(OBJ) build/cJSON.o
	$(CC) $(CFLAGS) -o $@ $(OBJ) build/cJSON.o -lcurl -lpthread $(LDLIBS_EXTRA)
ifeq ($(UNAME), Darwin)
	codesign -s - $@ 2>/dev/null || true
endif

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/cJSON.o: vendor/cJSON/cJSON.c | build
	$(CC) $(CFLAGS) -Wno-float-conversion -c -o $@ $<

build:
	mkdir -p build

# ── Test build ──────────────────────────────────────────────────────── #

test: build tmp
	$(CC) $(CFLAGS) -DTESTING -DTEST_TMP_DIR='"tmp"' -o build/test_runner \
		tests/test_runner.c tests/stubs.c $(TEST_SRC) $(TESTABLE_SRC) $(VENDOR_SRC) \
		-lcurl -lpthread 2>&1 || (echo "TEST BUILD FAILED" && exit 1)
	./build/test_runner 2>&1 | tee tmp/test-out.txt

# ── Clean ───────────────────────────────────────────────────────────── #

clean:
	rm -rf build/ relay tmp/

tmp:
	mkdir -p tmp

PERF_LOG ?= $(HOME)/relay/data/state/perf/profile.jsonl
PERF_REPORT_DIR ?= build/perf-report
PERF_BASELINE ?= $(PERF_REPORT_DIR)/report.json
PERF_PID ?=
PERF_DURATION ?= 30
PERF_CPU_DIR ?= build/perf-cpu

perf-cpu:
	@if [ -z "$(PERF_PID)" ]; then \
		echo "Usage: make perf-cpu PERF_PID=<pid> [PERF_DURATION=30]"; \
		exit 1; \
	fi
	./scripts/cpu_profile.sh --pid "$(PERF_PID)" \
		--duration "$(PERF_DURATION)" \
		--out "$(PERF_CPU_DIR)"

perf-report:
	mkdir -p $(PERF_REPORT_DIR)
	python3 scripts/perf_report.py \
		--input $(PERF_LOG) \
		--output-dir $(PERF_REPORT_DIR) \
		--baseline $(PERF_BASELINE)

# ── CI-parity check (GCC on macOS) ────────────────────────────────── #
# Mirrors the GitHub Actions ubuntu-latest build using Homebrew GCC.
# Run before pushing to catch GCC-specific warnings locally.

GCC    ?= $(shell ls /opt/homebrew/bin/gcc-1* 2>/dev/null | sort -V | tail -1)
CI_FLAGS = -Wall -Wextra -Werror -pedantic -std=c11 -D_DEFAULT_SOURCE \
           -Wno-format-truncation -Wno-stringop-truncation \
           -Iinclude -Ivendor -O2 -DRELAY_VERSION='"$(VERSION)"'

ci: clean build tmp
	@if [ -z "$(GCC)" ]; then echo "GCC not found. Run: brew install gcc"; exit 1; fi
	@echo "=== CI compile check (GCC, per-file) ==="
	@fail=0; for f in src/*.c; do \
		$(GCC) $(CI_FLAGS) -c -o /dev/null $$f 2>&1 && echo "  OK  $$f" || { echo "  FAIL $$f"; fail=1; }; \
	done; \
	echo ""; \
	if [ $$fail -eq 1 ]; then echo "CI check FAILED — fix errors above before pushing."; exit 1; \
	else echo "CI check PASSED — all source files compile clean with GCC."; fi
	@echo ""
	@echo "=== CI test build (GCC) ==="
	$(GCC) $(CI_FLAGS) -DTESTING -DTEST_TMP_DIR='"tmp"' -o build/test_runner \
		tests/test_runner.c tests/stubs.c $(TEST_SRC) $(TESTABLE_SRC) $(VENDOR_SRC) \
		-lcurl -lpthread 2>&1 || (echo "CI TEST BUILD FAILED" && exit 1)
	./build/test_runner 2>&1 | tee tmp/test-out.txt

.PHONY: test clean ci perf-cpu perf-report