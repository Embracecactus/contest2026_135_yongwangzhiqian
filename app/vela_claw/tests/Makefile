###############################################################################
# Vela-Claw host smoke test
#
# Builds the whole agent core with the mock transport (no NuttX / LVGL /
# libcurl) and verifies the agent loop converges. Run:  make -C tests
###############################################################################

CC      ?= gcc
CFLAGS  := -pthread -Wall -Wextra -O2 -I../include
LDFLAGS := -pthread

SRCS := \
  ../src/util/claw_json.c \
  ../src/platform/claw_rtos.c \
  ../src/platform/claw_event.c \
  ../src/storage/claw_storage.c \
  ../src/storage/claw_config.c \
  ../src/transport/claw_transport.c \
  ../src/transport/transport_mock.c \
  ../src/transport/transport_curl.c \
  ../src/core/claw_core.c \
  ../src/memory/claw_memory.c \
  ../src/router/claw_event_router.c \
  ../src/cap/claw_cap_registry.c \
  ../src/cap/cap_llm_config.c \
  ../src/cap/cap_files.c \
  ../src/cap/cap_lua.c \
  ../src/cap/cap_cli.c \
  ../src/cap/cap_system.c \
  ../src/cap/cap_mgrs.c \
  ../src/cap/cap_scheduler.c \
  ../src/cap/cap_http_request.c \
  ../src/cap/cap_web_search.c \
  ../src/lua/claw_lua_engine.c \
  ../src/lua/lua_driver_gpio.c \
  ../src/cli/claw_cli.c \
  ../src/vela_claw_init.c

TARGET := vela_claw_host_test

all: $(TARGET)

$(TARGET): $(SRCS) test_smoke.c
	$(CC) $(CFLAGS) -o $@ $(SRCS) test_smoke.c $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all run clean
