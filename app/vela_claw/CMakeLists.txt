############################################################################
# Vela-Claw — openvela/NuttX native "Chat Coding" AI agent
#
# The whole agent core (RTOS/transport/capability/router/memory) is
# RTOS-portable and compiles unchanged on NuttX and on the host. The screen
# UI (claw_ui.c) and the real curl transport are optional and gated by
# CONFIG_VELA_CLAW_UI / CONFIG_LIBCURL4NX respectively.
############################################################################

if(CONFIG_BK7258_APP_VELA_CLAW)

  set(SRCS
    src/util/claw_json.c
    src/platform/claw_rtos.c
    src/platform/claw_event.c
    src/storage/claw_storage.c
    src/storage/claw_config.c
    src/transport/claw_transport.c
    src/transport/transport_mock.c
    src/transport/transport_curl.c
    src/core/claw_core.c
    src/memory/claw_memory.c
    src/router/claw_event_router.c
    src/cap/claw_cap_registry.c
    src/cap/cap_llm_config.c
    src/cap/cap_files.c
    src/cap/cap_lua.c
    src/cap/cap_cli.c
    src/cap/cap_system.c
    src/cap/cap_mgrs.c
    src/cap/cap_scheduler.c
    src/cap/cap_http_request.c
    src/cap/cap_web_search.c
    src/lua/claw_lua_engine.c
    src/lua/lua_driver_gpio.c
    src/cli/claw_cli.c
    src/vela_claw_init.c)

  nuttx_add_library(vela_claw_lib STATIC ${SRCS})
  target_include_directories(vela_claw_lib BEFORE PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include)

  if(CONFIG_VELA_CLAW_UI)
    target_sources(vela_claw_lib PRIVATE src/ui/claw_ui.c)
  endif()

  # The real HTTP/TLS transport uses libcurl4nx when networking is enabled.
  if(CONFIG_LIBCURL4NX)
    target_compile_definitions(vela_claw_lib PRIVATE HAVE_LIBCURL)
  endif()

  nuttx_add_application(
    NAME ${CONFIG_BK7258_APP_VELA_CLAW_PROGNAME}
    STACKSIZE ${CONFIG_BK7258_APP_VELA_CLAW_STACKSIZE}
    PRIORITY ${CONFIG_BK7258_APP_VELA_CLAW_PRIORITY}
    SRCS src/vela_claw_main.c
    INCLUDE_DIRECTORIES ${CMAKE_CURRENT_LIST_DIR}/include
    DEPENDS vela_claw_lib)

endif()
