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
# a consumer argument in the same change. The one documented Session read
# safety fix below is staged from an exact pinned source into the build tree;
# it never modifies the official checkout or creates a second runtime.
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
    get_filename_component(entry_path "${source}/${unused_source}" ABSOLUTE)
    set(matched_sources)
    foreach(agent_source IN LISTS agent_sources)
      set(candidate "${agent_source}")
      if(NOT IS_ABSOLUTE "${candidate}")
        set(candidate "${source}/${candidate}")
      endif()
      get_filename_component(candidate "${candidate}" ABSOLUTE)
      if(candidate STREQUAL entry_path)
        list(APPEND matched_sources "${agent_source}")
      endif()
    endforeach()
    if(NOT matched_sources)
      message(FATAL_ERROR
        "Agent source blocklist entry '${unused_source}' no longer matches the "
        "pinned official target.  Review that official layout change and update "
        "this list deliberately; a silently relaxed filter is not an option.")
    endif()
    list(REMOVE_ITEM agent_sources ${matched_sources})
  endforeach()
  set_property(TARGET "${target}" PROPERTY SOURCES "${agent_sources}")
  list(LENGTH agent_sources kept_source_count)
  message(STATUS
    "Agent framework: filtered official sources, ${kept_source_count} kept")
  foreach(agent_source IN LISTS agent_sources)
    message(STATUS "  agent source: ${agent_source}")
  endforeach()

  # The Agent Session API uses the NuttX ERROR + errno convention.  Only an
  # absent history is empty; an unreadable or interrupted existing history
  # must reach the caller as an error so it cannot be published as a new,
  # empty product snapshot.  Stage and patch only the pinned upstream input:
  # source drift, a malformed patch, or a target layout change all stop
  # configuration rather than falling back to the unchecked source.
  set(session_source "${source}/src/core/session_mgr.c")
  set(session_patch
      "${NUTTX_DIR}/../vendor/beken/frameworks/patches/ai_agent/0001-session-history-read-errors.patch")
  set(session_stage "${CMAKE_BINARY_DIR}/ai-agent-session-history-stage")
  set(session_output "${CMAKE_BINARY_DIR}/ai-agent-session-history/src/core/session_mgr.c")
  set(session_source_sha256
      "132b0848384c89e4d7e15da63a3ecd5e5bc3914332adba0208d3eca474f4b93c")
  if(NOT EXISTS "${session_source}" OR NOT EXISTS "${session_patch}")
    message(FATAL_ERROR "Pinned Agent Session source or safety patch is missing")
  endif()
  file(SHA256 "${session_source}" actual_session_source_sha256)
  if(NOT "${actual_session_source_sha256}" STREQUAL "${session_source_sha256}")
    message(FATAL_ERROR
      "Pinned Agent Session source changed; review 0001-session-history-read-errors.patch")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${session_source}" "${session_patch}")
  file(REMOVE_RECURSE "${session_stage}")
  file(MAKE_DIRECTORY "${session_stage}/src/core")
  configure_file("${session_source}" "${session_stage}/src/core/session_mgr.c" COPYONLY)
  find_package(Git REQUIRED)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${CMAKE_BINARY_DIR}"
            "${GIT_EXECUTABLE}" apply --check "${session_patch}"
    WORKING_DIRECTORY "${session_stage}"
    RESULT_VARIABLE session_patch_check
    ERROR_VARIABLE session_patch_error)
  if(NOT session_patch_check EQUAL 0)
    message(FATAL_ERROR
      "Pinned Agent Session safety patch no longer applies: ${session_patch_error}")
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${CMAKE_BINARY_DIR}"
            "${GIT_EXECUTABLE}" apply "${session_patch}"
    WORKING_DIRECTORY "${session_stage}"
    RESULT_VARIABLE session_patch_apply
    ERROR_VARIABLE session_patch_error)
  if(NOT session_patch_apply EQUAL 0)
    message(FATAL_ERROR
      "Pinned Agent Session safety patch failed: ${session_patch_error}")
  endif()
  get_target_property(agent_root "${target}" SOURCE_DIR)
  set(updated_agent_sources)
  set(session_source_count 0)
  foreach(agent_source IN LISTS agent_sources)
    get_filename_component(candidate "${agent_source}" ABSOLUTE BASE_DIR "${agent_root}")
    if("${candidate}" STREQUAL "${session_source}")
      list(APPEND updated_agent_sources "${session_output}")
      math(EXPR session_source_count "${session_source_count} + 1")
    else()
      list(APPEND updated_agent_sources "${agent_source}")
    endif()
  endforeach()
  if(NOT session_source_count EQUAL 1)
    message(FATAL_ERROR
      "Expected one official Agent src/core/session_mgr.c, got ${session_source_count}")
  endif()
  get_filename_component(session_output_dir "${session_output}" DIRECTORY)
  file(MAKE_DIRECTORY "${session_output_dir}")
  configure_file("${session_stage}/src/core/session_mgr.c" "${session_output}" COPYONLY)
  set_property(TARGET "${target}" PROPERTY SOURCES "${updated_agent_sources}")
  message(STATUS "Agent framework: staged pinned Session history safety patch")

  target_include_directories(apps PRIVATE "${source}/include" "${source}/src")
  # The build identity is recorded by the manifest so that the official build
  # target does not touch source files.
  if(TARGET agent_touch_main)
    set_property(TARGET agent_touch_main PROPERTY EXCLUDE_FROM_ALL TRUE)
  endif()
endfunction()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}"
  CALL bk7258_configure_agent_framework)
