# SPDX-License-Identifier: Apache-2.0
# Keep the official source checkout unchanged; select only the official core
# modules this product actually uses.
#
# Coupling contract (recorded, not a license to patch): this file reaches
# into the official Agent target's SOURCES property, filters it by a
# concrete file-name blocklist and writes the property back, and it exposes
# the official src/ directory as an include path. That couples the product
# build to the pinned official checkout's internal layout, which is what
# the fixed NuttX commit in chips/bk7258/kernel_compat.json and the pinned
# packages_ai_agent revision in the manifest protect. Renaming or adding an
# official internal file changes what this filter matches; a drifted
# blocklist must fail loudly during review (cmake prints the filtered
# target's sources), never be silently relaxed. The exit condition is a
# minimal component-option or library interface negotiated with the
# official tree; until then no blocklist entry is removed or added without
# a consumer argument in the same change. Do not reinstate a patch overlay
# or a second agent runtime to work around this file.
function(bk7258_configure_agent_framework)
  set(source "${NUTTX_APPS_DIR}/packages/ai_agent")
  set(target "apps_${CONFIG_EXAMPLES_AI_AGENT_VELA_PROGNAME}")
  if(NOT TARGET "${target}")
    message(FATAL_ERROR "The full official Agent application is required")
  endif()

  # The current official contest branch unconditionally compiles the CLI,
  # WebSocket, cron service, heartbeat, network manager and all built-in tools
  # into the same target. Shaniu supplies its network, configuration and
  # interaction entry points from the product layer, so only the optional
  # services without a consumer are removed; the agent loop, message bus,
  # session, LLM, voice channel and Media keep the official implementation.
  # tool_files.c is kept: the product tool provider reads the runtime skill
  # documents (read_file) under /data/agent/skills/ with it, and is therefore
  # a consumer of the skill trigger path.
  get_target_property(agent_sources "${target}" SOURCES)
  set(unused_agent_sources
      src/agent_main.c
      src/core/message_bus_tap.c
      src/infra/network_manager.c
      src/infra/cron_service.c
      src/infra/heartbeat.c
      src/channels/nsh_commands.c
      src/channels/cmd_llm.c
      src/channels/cmd_voice.c
      src/channels/cmd_channel.c
      src/channels/ws_server.c
      src/tools/tool_get_time.c
      src/tools/tool_web_search.c
      src/tools/tool_cron.c
      src/tools/tool_fetch_url.c
      src/tools/tool_feishu_doc.c
      src/tools/tool_feishu_chat.c
      src/tools/tool_vision.c
      src/tools/tool_shell.c
      src/tools/tool_system.c
      src/tools/tool_health.c
      src/tools/tool_control.c
      src/tools/tool_media.c
      src/tools/tool_proxyquickapp.c
      src/tools/tool_amap.c
      src/ui/qrcode_display.c)
  foreach(unused_source IN LISTS unused_agent_sources)
    list(FILTER agent_sources EXCLUDE REGEX "(^|/)${unused_source}$")
  endforeach()
  set_property(TARGET "${target}" PROPERTY SOURCES "${agent_sources}")

  target_include_directories(apps PRIVATE "${source}/include" "${source}/src")
  # The build identity is recorded by the manifest so that the official build
  # target does not touch source files.
  if(TARGET agent_touch_main)
    set_property(TARGET agent_touch_main PROPERTY EXCLUDE_FROM_ALL TRUE)
  endif()
endfunction()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}"
  CALL bk7258_configure_agent_framework)
