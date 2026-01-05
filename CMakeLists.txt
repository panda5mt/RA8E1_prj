# Please see README.md for build instructions

cmake_minimum_required(VERSION 3.16.4)

set(CMAKE_SYSTEM_NAME Generic)

set(CMAKE_CONFIGURATION_TYPES Debug Release RelWithDebInfo MinSizeRel)

cmake_minimum_required(VERSION 3.16.4)

# Project configuration
project(RA8E1_prj
	VERSION 1.0.0
	LANGUAGES C CXX ASM)

set(CMAKE_CROSSCOMPILING "TRUE")
set(CMAKE_EXPORT_COMPILE_COMMANDS "TRUE")

include(${CMAKE_CURRENT_LIST_DIR}/Config.cmake)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/GeneratedCfg.cmake)

# Enable Helium MVE for ARM Cortex-M85
list(APPEND RASC_CMAKE_C_FLAGS "-march=armv8.1-m.main+mve")
list(APPEND RASC_CMAKE_CXX_FLAGS "-march=armv8.1-m.main+mve")

# Speed-first tuning:
# RASC base flags include -Os (size optimization), which can hurt DSP/FFT performance.
# Override to prefer -O3 for compute-heavy workloads.
list(REMOVE_ITEM RASC_CMAKE_C_FLAGS "-Os")
list(REMOVE_ITEM RASC_CMAKE_CXX_FLAGS "-Os")
list(REMOVE_ITEM RASC_DEBUG_FLAGS "-O2")
list(APPEND RASC_DEBUG_FLAGS "-O3")
list(REMOVE_ITEM RASC_RELEASE_FLAGS "-O2")
list(APPEND RASC_RELEASE_FLAGS "-O3")
list(REMOVE_ITEM RASC_RELEASE_WITH_DEBUG_INFO "-O2")
list(APPEND RASC_RELEASE_WITH_DEBUG_INFO "-O3")

# If required user can amend RASC generated flags here, e.g. list(REMOVE_ITEM RASC_CMAKE_C_FLAGS "-Wall")

include(${CMAKE_CURRENT_LIST_DIR}/cmake/GeneratedSrc.cmake)

# CMSIS-DSP enables float16 kernels when __ARM_FP16_FORMAT_IEEE is defined.
# Renesas LLVM clang does not accept -mfp16-format=ieee, so we define the macro directly.
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE __ARM_FP16_FORMAT_IEEE)

# Add xprintf include path
target_include_directories(${PROJECT_NAME}.elf
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/xprintf/src
)
