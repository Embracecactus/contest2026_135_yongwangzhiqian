# SPDX-License-Identifier: Apache-2.0
# Build the official provider with the team's request extension in the build
# tree. The checked-out package remains unchanged.

function(bk7258_add_agent_provider)
  set(source "${NUTTX_APPS_DIR}/packages/ai_agent")
  set(stage "${CMAKE_BINARY_DIR}/ai-agent-provider-stage")
  set(output "${CMAKE_BINARY_DIR}/ai-agent-provider")
  set(patch "${NUTTX_DIR}/../vendor/beken/frameworks/patches/ai_agent/0001-request-scoped-provider.patch")
  file(GLOB_RECURSE headers CONFIGURE_DEPENDS
       RELATIVE "${source}" "${source}/include/*.h" "${source}/src/*.h")
  # Stage only files that exist in the unmodified official package.  The
  # patch creates agent_turn.[ch], then output_files copies those results.
  set(input_files CMakeLists.txt Kconfig src/llm/llm_proxy.c
      src/llm/llm_parse.c src/core/agent_loop.c ${headers})
  set(output_files ${input_files} src/core/agent_turn.c src/core/agent_turn.h)
  # A prior configure may have already applied the patch.  Recreate only the
  # staging tree so apply --check observes pristine official inputs; preserve
  # output/build and its generated targets for incremental builds.
  file(REMOVE_RECURSE "${stage}")
  foreach(relative IN LISTS input_files)
    get_filename_component(directory "${stage}/${relative}" DIRECTORY)
    file(MAKE_DIRECTORY "${directory}")
    configure_file("${source}/${relative}" "${stage}/${relative}" COPYONLY)
  endforeach()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${patch}")
  find_package(Git REQUIRED)
  execute_process(COMMAND "${GIT_EXECUTABLE}" apply --check "${patch}"
                  WORKING_DIRECTORY "${stage}" RESULT_VARIABLE result
                  ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Official Agent provider patch no longer applies: ${error}")
  endif()
  execute_process(COMMAND "${GIT_EXECUTABLE}" apply "${patch}"
                  WORKING_DIRECTORY "${stage}" RESULT_VARIABLE result
                  ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Official Agent provider patch failed: ${error}")
  endif()
  foreach(relative IN LISTS output_files)
    get_filename_component(directory "${output}/${relative}" DIRECTORY)
    file(MAKE_DIRECTORY "${directory}")
    configure_file("${stage}/${relative}" "${output}/${relative}" COPYONLY)
  endforeach()
  # Product Kconfig owns selection until the package extension is upstream.
  # The official CMake library owns its translation units and dependencies.
  set(CONFIG_AI_AGENT_LLM_REQUEST ON)
  add_subdirectory("${output}" "${output}/build")
  target_link_libraries(apps PRIVATE ai_agent_llm)
  target_include_directories(apps PRIVATE "${output}/include" "${output}/src"
    "${NUTTX_APPS_DIR}/netutils/cjson/cJSON")
endfunction()

bk7258_add_agent_provider()
