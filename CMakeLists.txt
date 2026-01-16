# Please see README.md for build instructions

cmake_minimum_required(VERSION 3.16.4)

set(CMAKE_SYSTEM_NAME Generic)

# CMAKE_CONFIGURATION_TYPES is only meaningful for multi-config generators
# (e.g. Ninja Multi-Config / Visual Studio). For single-config generators
# like Ninja, the build type is controlled by CMAKE_BUILD_TYPE.
get_property(_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(_is_multi_config)
	set(CMAKE_CONFIGURATION_TYPES "Debug;Release;RelWithDebInfo;MinSizeRel" CACHE STRING "Supported build types" FORCE)
endif()

# Project configuration
project(RA8E1_prj
	VERSION 1.0.0
	LANGUAGES C CXX ASM)

set(CMAKE_CROSSCOMPILING "TRUE")
set(CMAKE_EXPORT_COMPILE_COMMANDS "TRUE")

include(${CMAKE_CURRENT_LIST_DIR}/Config.cmake)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/GeneratedCfg.cmake)

# ---- Guard for RASC pre/post build steps ----
#
# GeneratedSrc.cmake registers RASC pre-build and post-build commands when it thinks we are
# not running inside e2studio (it checks RENESAS_IDE).
#
# On some ARM64 hosts, RASC may not be available. However, on Windows x64 (and on any host
# where RASC is installed), these steps should run.
#
# So we only disable the pre/post steps when BOTH:
# - the host CPU is ARM64/aarch64, AND
# - RASC_EXE_PATH is not set or does not exist.
option(APP_DISABLE_RASC_PREPOST_WHEN_UNAVAILABLE "Disable RASC pre/post build steps when RASC is unavailable (ARM64 hosts)" ON)
if (APP_DISABLE_RASC_PREPOST_WHEN_UNAVAILABLE)
	if (CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
		set(_rasc_available OFF)
		if (DEFINED RASC_EXE_PATH AND (NOT "${RASC_EXE_PATH}" STREQUAL "") AND EXISTS "${RASC_EXE_PATH}")
			set(_rasc_available ON)
		endif()
		if (NOT _rasc_available)
			# Make GeneratedSrc.cmake treat this as e2studio so it does not register the
			# pre-build and post-build RASC commands.
			set(RENESAS_IDE "e2studio" CACHE STRING "Forced on ARM64 host when RASC is unavailable" FORCE)
		endif()
		unset(_rasc_available)
	endif()
endif()

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

# ---- Optional: build CMSIS-NN only when needed ----
# GeneratedSrc.cmake uses a broad GLOB over ra/*.c which pulls in CMSIS-NN sources.
# That is convenient but expensive; keep CMSIS-NN opt-in by filtering its sources out
# of the final target unless explicitly enabled.
option(APP_ENABLE_CMSIS_NN "Build CMSIS-NN (ra/arm/CMSIS-NN/Source)" OFF)
if (NOT APP_ENABLE_CMSIS_NN)
	get_target_property(_app_sources ${PROJECT_NAME}.elf SOURCES)
	if (_app_sources)
		# Match both '/' and '\\' path separators.
		list(FILTER _app_sources EXCLUDE REGEX "[/\\\\]CMSIS-NN[/\\\\]Source[/\\\\].*\\.(c|cc|cpp|cxx|S|asm|sx|msa)$")
		set_property(TARGET ${PROJECT_NAME}.elf PROPERTY SOURCES "${_app_sources}")
	endif()
	unset(_app_sources)
endif()

# Optional: Release-only LTO for additional speed (can help inlining across translation units).
# Keep this opt-in/disableable because some toolchains/linker setups may not support LTO.
option(APP_ENABLE_LTO "Enable link-time optimization (Release only)" ON)
if (APP_ENABLE_LTO)
	target_compile_options(${PROJECT_NAME}.elf PRIVATE $<$<CONFIG:Release>:-flto>)
	target_link_options(${PROJECT_NAME}.elf PRIVATE $<$<CONFIG:Release>:-flto>)
endif()

# CMSIS-DSP enables float16 kernels when __ARM_FP16_FORMAT_IEEE is defined.
# Renesas LLVM clang does not accept -mfp16-format=ieee, so we define the macro directly.
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE __ARM_FP16_FORMAT_IEEE)

# Optional: run FFT verification (thread3) while keeping camera + UDP running.
# This is useful for coexistence testing under real bus contention.
option(APP_FFT_VERIFY_COEXIST "Run FFT verify without stopping camera/UDP" OFF)
if (APP_FFT_VERIFY_COEXIST)
	target_compile_definitions(${PROJECT_NAME}.elf PRIVATE
		APP_MODE_FFT_VERIFY=1
		APP_MODE_FFT_VERIFY_DISABLE_CAMERA=0
		APP_MODE_FFT_VERIFY_DISABLE_UDP=0
	)
endif()

# Add xprintf include path
target_include_directories(${PROJECT_NAME}.elf
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/xprintf/src
)
