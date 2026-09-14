# SPDX-License-Identifier: Apache-2.0
# The product's TFLM reference kernels use Ruy's instrumentation component,
# not its desktop matrix engine/thread pool (see TFLM internal/BUILD).
# The official Make integration likewise supplies Ruy headers only. Select
# that upstream component explicitly for this CMake product configuration.
function(bkvoice_select_tflm_ruy_component)
  set(instrumentation
      "${NUTTX_APPS_DIR}/math/ruy/ruy/ruy/profiler/instrumentation.cc")
  if(NOT TARGET ruy OR NOT EXISTS "${instrumentation}")
    message(FATAL_ERROR "TFLM requires the manifest Ruy instrumentation component")
  endif()
  set_property(TARGET ruy PROPERTY SOURCES "${instrumentation}")
  # The shared AP slot also contains Media and networking. Use the product's
  # size policy instead of TFLM's unconditional -O3; measure latency on target.
  target_compile_options(tflite_micro PRIVATE -Os)
endfunction()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}"
               CALL bkvoice_select_tflm_ruy_component)
