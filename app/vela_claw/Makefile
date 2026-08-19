############################################################################
# Vela-Claw — openvela/NuttX native "Chat Coding" AI agent (classic Make)
############################################################################

include $(APPDIR)/Make.defs

# Make the app's own include/ directory (e.g. claw_common.h) visible to all
# translation units of this app.
CFLAGS   += ${INCDIR_PREFIX}$(APPDIR)/packages/demos/contest2026_135_vela_claw/include
CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/packages/demos/contest2026_135_vela_claw/include

ifeq ($(CONFIG_BK7258_APP_VELA_CLAW),y)
# The build configures .config via tools/process_config.sh (a plain text
# concatenator), which does NOT run the Kconfig constraint solver. Kconfig
# defaults for PROGNAME/PRIORITY/STACKSIZE are therefore never written into
# .config, and `make savedefconfig` strips them from the defconfig too.
# Provide explicit fallbacks so the app always registers as an NSH builtin
# even when the Kconfig symbols are left at their defaults.
PROGNAME  = $(CONFIG_BK7258_APP_VELA_CLAW_PROGNAME)
ifeq ($(PROGNAME),)
PROGNAME  = vela_claw
endif
PRIORITY  = $(CONFIG_BK7258_APP_VELA_CLAW_PRIORITY)
ifeq ($(PRIORITY),)
PRIORITY  = 100
endif
STACKSIZE = $(CONFIG_BK7258_APP_VELA_CLAW_STACKSIZE)
ifeq ($(STACKSIZE),)
STACKSIZE = 8192
endif
MAINSRC   += src/vela_claw_main.c

CSRCS += src/util/claw_json.c
CSRCS += src/platform/claw_rtos.c
CSRCS += src/platform/claw_event.c
CSRCS += src/storage/claw_storage.c
CSRCS += src/storage/claw_config.c
CSRCS += src/transport/claw_transport.c
CSRCS += src/transport/transport_mock.c
CSRCS += src/transport/transport_curl.c
CSRCS += src/core/claw_core.c
CSRCS += src/memory/claw_memory.c
CSRCS += src/router/claw_event_router.c
CSRCS += src/cap/claw_cap_registry.c
CSRCS += src/cap/cap_llm_config.c
CSRCS += src/cap/cap_files.c
CSRCS += src/cap/cap_lua.c
CSRCS += src/cap/cap_cli.c
CSRCS += src/cap/cap_system.c
CSRCS += src/cap/cap_mgrs.c
CSRCS += src/cap/cap_scheduler.c
CSRCS += src/cap/cap_http_request.c
CSRCS += src/cap/cap_web_search.c
CSRCS += src/lua/claw_lua_engine.c
CSRCS += src/lua/lua_driver_gpio.c
CSRCS += src/cli/claw_cli.c
CSRCS += src/vela_claw_init.c
endif

ifeq ($(CONFIG_VELA_CLAW_UI),y)
CSRCS += src/ui/claw_ui.c
endif

ifeq ($(CONFIG_LIBCURL4NX),y)
CFLAGS += -DHAVE_LIBCURL
endif

include $(APPDIR)/Application.mk
