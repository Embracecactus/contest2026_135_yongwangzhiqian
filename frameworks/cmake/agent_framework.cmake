# SPDX-License-Identifier: Apache-2.0
# 保持官方源码检出不变，只选择本产品实际使用的官方核心模块。
function(bk7258_configure_agent_framework)
  set(source "${NUTTX_APPS_DIR}/packages/ai_agent")
  set(target "apps_${CONFIG_EXAMPLES_AI_AGENT_VELA_PROGNAME}")
  if(NOT TARGET "${target}")
    message(FATAL_ERROR "The full official Agent application is required")
  endif()

  # 当前官方比赛分支把 CLI、WebSocket、定时任务、心跳、网络接管和全部
  # 内置工具无条件编入同一目标。傻妞由产品层提供网络、配置和交互入口，
  # 这里只从目标中移除没有消费者的可选服务；Agent loop、消息总线、会话、
  # LLM、voice channel 与 Media 仍使用官方实现。
  # tool_files.c 保留：产品工具 provider 用它读取 /data/agent/skills/ 下的
  # 运行时技能文档（read_file），是技能触发链路的消费者。
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
  # 构建身份由 manifest 记录，避免官方构建目标 touch 源文件。
  if(TARGET agent_touch_main)
    set_property(TARGET agent_touch_main PROPERTY EXCLUDE_FROM_ALL TRUE)
  endif()
endfunction()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}"
  CALL bk7258_configure_agent_framework)
