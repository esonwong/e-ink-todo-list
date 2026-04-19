# Host-side unit tests (G10) — plain cc, runnable on macOS / Linux.
#
#   make            build + run every suite
#   make endpoint   build + run endpoint_test only
#   make urlencode  build + run urlencode_test only
#   make transform  build + run transform_test only
#   make clean
#
# The device bakes its fallback candidates in via Kconfig; here we inject the
# same values with -D so the build_candidates / prioritize / remember paths
# are exercised with two real fallbacks. Hostnames stay distinct in their
# first 15 chars so the truncated ids don't collide (see endpoint_test.c).

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -O0 -g

HERE      := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
RTOS      := $(abspath $(HERE)/../..)
SUPPORT   := $(HERE)support
ENDPOINT  := $(RTOS)/components/endpoint_fallback
TODOFETCH := $(RTOS)/components/todo_fetch

# 40 hex = SHA-1, colon-separated (legacy fingerprint shape).
FB_PIN_1 := AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD
# 64 hex = SHA-256.
FB_PIN_2 := 00112233445566778899AABBCCDDEEFF00112233445566778899AABBCCDDEEFF

ENDPOINT_DEFS := \
  -DCONFIG_EINKTODO_FALLBACK_URL_1='"https://fb1.example.com"' \
  -DCONFIG_EINKTODO_FALLBACK_PIN_1='"$(FB_PIN_1)"' \
  -DCONFIG_EINKTODO_FALLBACK_URL_2='"https://fb2.example.com"' \
  -DCONFIG_EINKTODO_FALLBACK_PIN_2='"$(FB_PIN_2)"'

BIN := $(HERE)bin

.PHONY: all endpoint urlencode transform clean
all: endpoint urlencode transform

$(BIN):
	@mkdir -p $(BIN)

endpoint: $(BIN)
	$(CC) $(CFLAGS) \
	  -I$(HERE) -I$(SUPPORT) -I$(ENDPOINT)/include \
	  $(ENDPOINT_DEFS) \
	  $(HERE)endpoint_test.c \
	  $(ENDPOINT)/endpoint_fallback.c \
	  $(SUPPORT)/nvs_mock.c \
	  -o $(BIN)/endpoint_test
	$(BIN)/endpoint_test

urlencode: $(BIN)
	$(CC) $(CFLAGS) \
	  -I$(HERE) -I$(TODOFETCH) \
	  $(HERE)urlencode_test.c \
	  $(TODOFETCH)/urlencode.c \
	  -o $(BIN)/urlencode_test
	$(BIN)/urlencode_test

transform: $(BIN)
	$(CC) $(CFLAGS) \
	  -I$(HERE) \
	  $(HERE)transform_test.c \
	  -o $(BIN)/transform_test
	$(BIN)/transform_test

clean:
	rm -rf $(BIN)
