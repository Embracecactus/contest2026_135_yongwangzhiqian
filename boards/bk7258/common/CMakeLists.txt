# NuttX board-level CMake for the Beken BK7258 logical board.

foreach(BK7258_REQUIRED_INPUT
        BK7258_SDK_DIR
        BK7258_PARTITION_HEADER
        BK7258_PARTITION_LINKER
        BK7258_PARTITION_SDK_CSV
        BK7258_PARTITION_ID
        BK7258_PARTITION_SHA256)
  if("$ENV{${BK7258_REQUIRED_INPUT}}" STREQUAL "")
    message(FATAL_ERROR
            "${BK7258_REQUIRED_INPUT} is required; use tools/bk7258/bk7258.py build")
  endif()
endforeach()

set(BK7258_SDK_DIR "$ENV{BK7258_SDK_DIR}")
set(BK7258_SDK_INCLUDE_DIR "${BK7258_SDK_DIR}/include")
set(BK7258_SDK_CONFIG_DIR "${BK7258_SDK_DIR}/config")
set(BK7258_SDK_LIBS_DIR "${BK7258_SDK_DIR}/libs")
set(BK7258_PARTITION_HEADER "$ENV{BK7258_PARTITION_HEADER}")
set(BK7258_PARTITION_LINKER "$ENV{BK7258_PARTITION_LINKER}")
set(BK7258_PARTITION_LAYOUT_ID "$ENV{BK7258_PARTITION_ID}")
set(BK7258_PARTITION_LAYOUT_SHA256 "$ENV{BK7258_PARTITION_SHA256}")
if(CONFIG_BK7258_AP_CORE)
  set(BK7258_PARTITION_ROLE ap)
else()
  set(BK7258_PARTITION_ROLE cp)
endif()
get_filename_component(BK7258_PARTITION_INCLUDE_DIR
                       "${BK7258_PARTITION_HEADER}" DIRECTORY)

foreach(BK7258_REQUIRED_PATH
        "${BK7258_SDK_INCLUDE_DIR}"
        "${BK7258_SDK_CONFIG_DIR}"
        "${BK7258_SDK_LIBS_DIR}"
        "${BK7258_PARTITION_HEADER}"
        "${BK7258_PARTITION_LINKER}")
  if(NOT EXISTS "${BK7258_REQUIRED_PATH}")
    message(FATAL_ERROR "BK7258 build input is missing: ${BK7258_REQUIRED_PATH}")
  endif()
endforeach()

set_property(TARGET nuttx APPEND PROPERTY NUTTX_INCLUDE_DIRECTORIES
             "${BK7258_PARTITION_INCLUDE_DIR}")
nuttx_cpp_compile_options("-I${BK7258_PARTITION_INCLUDE_DIR}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${BK7258_PARTITION_HEADER}"
             "${BK7258_PARTITION_LINKER}")

set(BK7258_BOARD_VARIANT_COUNT 0)
foreach(BK7258_BOARD_SELECTOR
        CONFIG_BK7258_BOARD_AIDK_AI_TOY
        CONFIG_BK7258_BOARD_T5_BOARD
        CONFIG_BK7258_BOARD_T5AI_CORE)
  if(${BK7258_BOARD_SELECTOR})
    math(EXPR BK7258_BOARD_VARIANT_COUNT
         "${BK7258_BOARD_VARIANT_COUNT} + 1")
  endif()
endforeach()

if(NOT BK7258_BOARD_VARIANT_COUNT EQUAL 1)
  message(FATAL_ERROR "Select exactly one BK7258 physical board")
endif()

# board.h is consumed by board and architecture compilation units.  Keep the
# selected carrier header on both include paths; the classic Make backend
# mirrors this in scripts/Make.defs.

set(BK7258_BOARD_VARIANT_DIR "${NUTTX_BOARD_DIR}")
set(BK7258_BOARD_COMMON_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_include_directories(board PUBLIC
  "${BK7258_BOARD_VARIANT_DIR}/include"
  "${BK7258_BOARD_COMMON_INCLUDE}")
target_include_directories(arch PUBLIC
  "${BK7258_BOARD_VARIANT_DIR}/include"
  "${BK7258_BOARD_COMMON_INCLUDE}")

# Applications include the selected public board interface through
# <arch/board/board.h>.  Export the logical board's shared declarations via
# NuttX's application interface so that board.h remains self-contained for
# every application compilation unit.

nuttx_include_directories_for_all_apps("${BK7258_BOARD_COMMON_INCLUDE}")

add_subdirectory(src)

# Keep the immutable SDK Flash mechanism in the chip layer while replacing
# its build-time partition table with the product layout owned by this logical
# board.  CP is the only Flash owner and the matching wrappers live in the
# board archive.

if(NOT CONFIG_BK7258_AP_CORE)
  foreach(BK7258_PARTITION_SYMBOL
          bk_flash_partition_get_info
          bk_flash_partition_read
          bk_flash_partition_write
          bk_flash_partition_erase
          bk_flash_partition_write_perm_check_by_addr)
    target_link_options(nuttx PRIVATE
                        "LINKER:--wrap=${BK7258_PARTITION_SYMBOL}")
  endforeach()
endif()

# Pin the role-specific linker script for the CMake backend.  The generated
# wrapper embeds the layout identity, so CMake's normal source dependency
# rebuilds the preprocessed script whenever the selected contract changes.
if(CONFIG_BK7258_AP_CORE)
  set(BK7258_LDSCRIPT ld_ap.script)
else()
  set(BK7258_LDSCRIPT ld.script)
endif()

set(BK7258_LDSCRIPT_SOURCE
    "${CMAKE_CURRENT_LIST_DIR}/scripts/${BK7258_LDSCRIPT}")
set(BK7258_LDSCRIPT_WRAPPER
    "${CMAKE_BINARY_DIR}/bk7258-${BK7258_PARTITION_ROLE}-link.ld")
file(CONFIGURE OUTPUT "${BK7258_LDSCRIPT_WRAPPER}" CONTENT
     "/* BK7258 partition ${BK7258_PARTITION_LAYOUT_ID} ${BK7258_PARTITION_LAYOUT_SHA256} */\nINCLUDE \"${BK7258_PARTITION_LINKER}\"\n#include \"${BK7258_LDSCRIPT_SOURCE}\"\n"
     @ONLY NEWLINE_STYLE UNIX)
set_property(GLOBAL PROPERTY LD_SCRIPT
             "${BK7258_LDSCRIPT_WRAPPER}")

# sdk rebuild resolves the profile-specific closure.  The board links every
# resulting input without repeating vendor library names or feature maps.
file(GLOB BK7258_SDK_LINK_INPUTS CONFIGURE_DEPENDS
     "${BK7258_SDK_LIBS_DIR}/*.a"
     "${BK7258_SDK_LIBS_DIR}/*.o"
     "${BK7258_SDK_LIBS_DIR}/*.obj")
list(SORT BK7258_SDK_LINK_INPUTS)
if(NOT BK7258_SDK_LINK_INPUTS)
  message(FATAL_ERROR "BK7258 SDK profile has no resolved link inputs")
endif()
foreach(BK7258_SDK_LINK_INPUT ${BK7258_SDK_LINK_INPUTS})
  nuttx_add_extra_library("${BK7258_SDK_LINK_INPUT}")
endforeach()
