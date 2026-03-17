cmake_minimum_required (VERSION 3.15)

# minimum macOS deployment target; must come before project()!
if (APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum OSX deployment version")
endif()

set(aoo_version_major 2)
set(aoo_version_minor 0)
set(aoo_version_patch 0)
set(aoo_version ${aoo_version_major}.${aoo_version_minor}.${aoo_version_patch})

message(STATUS "Project: aoo ${aoo_version}")
project(aoo VERSION ${aoo_version})

include(GNUInstallDirs)
include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)
include(cmake/test_atomic.cmake)
include(cmake/test_linker_flag.cmake)

message(STATUS "")
message(STATUS "*** Global settings ***")
message(STATUS "")

if (UNIX AND NOT APPLE AND NOT MINGW)
    set(LINUX TRUE)
endif()

# some MinGW setups don't define WIN32!
if (MINGW AND NOT WIN32)
    message(WARNING "Your MinGW setup does not define WIN32")
    set(WIN32 TRUE)
endif()

# check for Clang or AppleClang
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_COMPILER_IS_CLANG 1)
endif()
message(STATUS "CMAKE_CXX_COMPILER_ID: ${CMAKE_CXX_COMPILER_ID}")

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

message(STATUS "CMAKE_INSTALL_PREFIX: ${CMAKE_INSTALL_PREFIX}")

# Windows paths
if (WIN32)
    # check if "Program Files (x86)" exists (64-bit Windows) and if we compile for 32-bit
    set(_pf_x86 "ProgramFiles(x86)")
    if (DEFINED ENV{${_pf_x86}} AND (CMAKE_SIZEOF_VOID_P EQUAL 4))
        set(PROGRAMFILES $ENV{${_pf_x86}})
    else()
        set(PROGRAMFILES $ENV{PROGRAMFILES})
    endif()
    set(APPDATA $ENV{APPDATA})
    set(LOCALAPPDATA $ENV{LOCALAPPDATA})
endif()

# C++17 required
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT APPLE)
    set(CMAKE_INSTALL_RPATH $ORIGIN/../lib)
endif()

if (LINUX)
    set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)
endif()

# built everything (including Opus) with -fPIC
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if (MINGW)
    set(CMAKE_EXECUTABLE_SUFFIX ".exe")
endif()

if (CMAKE_COMPILER_IS_GNUCXX)
    if (MINGW)
        option(AOO_STATIC_RUNTIME "link with static runtime libraries (libstdc++, libgcc and libphread)" ON)
    else()
        option(AOO_STATIC_RUNTIME "link with static runtime libraries (libstdc++ and libgcc)" OFF)
    endif()
    # TODO: should AOO_STATIC_RUNTIME try to link with a static libopus?
    message(STATUS "Build with static runtime libraries: ${AOO_STATIC_RUNTIME}")
endif()

#-----------------------------------------------------------------
# feature tests
#-----------------------------------------------------------------
if (CMAKE_COMPILER_IS_GNUCXX OR CMAKE_COMPILER_IS_CLANG)
    check_cxx_compiler_flag("-msse" HAVE_CXX_SSE)
    check_cxx_compiler_flag("-msse2" HAVE_CXX_SSE2)
    check_cxx_compiler_flag("-msse3" HAVE_CXX_SSE3)
    check_cxx_compiler_flag("-msse4" HAVE_CXX_SSE4)
    check_cxx_compiler_flag("-mfpmath=sse" HAVE_CXX_FPMATH_SSE)

    test_linker_flag("-latomic" AOO_HAVE_LIB_ATOMIC)

    # atomic double support
    test_atomic(double AOO_HAVE_ATOMIC_DOUBLE)
    if (NOT AOO_HAVE_ATOMIC_DOUBLE)
        message(STATUS "No built-in support for atomic doubles - "
            "will be emulated in software")
    endif()

    # atomic 64-bit integer support
    test_atomic(int64 AOO_HAVE_ATOMIC_INT64)
    if (NOT AOO_HAVE_ATOMIC_INT64)
        message(STATUS "No built-in support for atomic 64-bit integers - "
            "will be emulated in software")
    endif()

    # pthread_rwlock_t support
    if (NOT WIN32)
        if (NOT DEFINED AOO_HAVE_PTHREAD_RWLOCK)
            message(STATUS "Testing support for pthread_rwlock_t")
            if (NOT ESP_PLATFORM)
                set(_PTHREAD_LIB "pthread")
            endif()

            try_compile(RESULT_VAR
                "${CMAKE_CURRENT_BINARY_DIR}"
                "${CMAKE_CURRENT_SOURCE_DIR}/cmake/pthread_rwlock.cpp"
                OUTPUT_VARIABLE COMPILE_OUTPUT
                CXX_STANDARD 17
                LINK_LIBRARIES ${_PTHREAD_LIB})

            if (RESULT_VAR)
                message(STATUS "- ok")
            else()
                message(STATUS "- failed")
                message(VERBOSE ${COMPILE_OUTPUT})
            endif()

            set(AOO_HAVE_PTHREAD_RWLOCK ${RESULT_VAR} CACHE INTERNAL "pthread_rwlock_t support")
        endif()
    endif()
endif()

#-----------------------------------------------------------------
# build options
#-----------------------------------------------------------------

option(AOO_USE_OPUS "use Opus codec" ON)

option(AOO_BUILD_DOCUMENTATION "build API documentation" OFF)
option(AOO_BUILD_EXAMPLES "build examples" OFF)
option(AOO_BUILD_PD_EXTERNAL "build Pure Data external" OFF)
option(AOO_BUILD_SC_EXTENSION "build SuperCollider extension" OFF)
option(AOO_BUILD_SERVER "build AOO server program" OFF)
option(AOO_BUILD_SHARED_LIBRARY "build shared AOO library" OFF)
option(AOO_BUILD_TESTS "build test suite" OFF)
option(AOO_INSTALL_LIBRARY "install the AOO library" ON)
option(AOO_INSTALL_PKG_CONFIG_MODULE "install pkg-config module" ON)
option(AOO_INSTALL_CMAKE_CONFIG_MODULE "install CMake package config module" ON)

if (AOO_BUILD_SHARED_LIBRARY OR BUILD_SHARED_LIBS)
    # propagate to dependencies
    set(BUILD_SHARED_LIBS ON)
    set(AOO_BUILD_SHARED_LIBRARY ON)
endif()

#-----------------------------------------------------------------
# dependencies
#-----------------------------------------------------------------

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Opus
if (AOO_USE_OPUS)
    option(AOO_LOCAL_OPUS "use local Opus library" ON)
    if (AOO_LOCAL_OPUS)
        message(STATUS "")
        message(STATUS "*** Opus library ***")
        message(STATUS "")

        add_subdirectory("deps/opus" EXCLUDE_FROM_ALL)
        if (NOT BUILD_SHARED_LIBS)
            # HACK: make sure that we don't export any symbols
            target_compile_definitions(opus PRIVATE OPUS_EXPORT=extern)
        endif()
    else()
        message(STATUS "Find Opus")
        find_package(Opus REQUIRED)
        message(STATUS "- ok")
    endif()
endif()

if (AOO_BUILD_EXAMPLES)
    option(AOO_LOCAL_PORTAUDIO "use local portaudio library" ON)
    if (AOO_LOCAL_PORTAUDIO)
        message(STATUS "")
        message(STATUS "*** portaudio library ***")
        message(STATUS "")

        # for portaudio v19.8 <=
        set(PA_BUILD_SHARED_LIBS OFF)
        # for portaudio v19.7 >=
        set(PA_BUILD_SHARED OFF)
        add_subdirectory("deps/portaudio" EXCLUDE_FROM_ALL)
        if (NOT TARGET PortAudio::portaudio)
            # at the time of writing, portaudio does not export
            # PortAudio::portaudio when included as a subproject.
            if (TARGET portaudio)
                # portaudio v19.8 <=
                add_library(PortAudio::portaudio ALIAS portaudio)
            elseif (TARGET portaudio_static)
                # portaudio v19.7 >=
                add_library(PortAudio::portaudio ALIAS portaudio_static)
            else()
                message(FATAL_ERROR "missing portaudio target")
            endif()
        endif()
    else()
        message(STATUS "Find PortAudio")
        find_package(PortAudio REQUIRED)
        message(STATUS "- ok")
    endif()
endif()

#-----------------------------------------------------------------
# hide all symbols unless explicitly exported
# NOTE: do this *after* Opus to avoid warnings
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

#-------------------------------------------------------------
# AOO compile time options
#-------------------------------------------------------------

message(STATUS "")
message(STATUS "*** AOO compile time options ***")
message(STATUS "")

if (CMAKE_COMPILER_IS_GNUCXX)
    # report stack usage
    option(AOO_STACK_USAGE "report stack usage" OFF)
    mark_as_advanced(AOO_STACK_USAGE)
endif()

# sample size
set(AOO_SAMPLE_SIZE 32 CACHE STRING "Audio sample size")
set_property(CACHE AOO_SAMPLE_SIZE PROPERTY STRINGS "32;64")
message(STATUS "Audio sample size: ${AOO_SAMPLE_SIZE} bits")

# logging
set(AOO_LOG_LEVEL "Warning" CACHE STRING "compile time log level")
set_property(CACHE AOO_LOG_LEVEL PROPERTY STRINGS "Silent;Error;Warning;Info;Debug;Verbose")
message(STATUS "Log level: ${AOO_LOG_LEVEL}")

# opus support
message(STATUS "Use Opus codec: ${AOO_USE_OPUS}")

# networking support
option(AOO_NET "Build with internal networking support" ON)
message(STATUS "Use internal networking (AOO NET): ${AOO_NET}")

# IPv6 support
# NB: set this on the top level because it is required by
# common/net_utils.h, which is also used in the Pd external.
option(AOO_USE_IPV6 "Build with IPv6 support" ON)
mark_as_advanced(AOO_USE_IPV6)
message(STATUS "IPv6 support: ${AOO_USE_IPV6}")

# compile time options
option(AOO_CUSTOM_ALLOCATOR "build with custom allocator support" OFF)
mark_as_advanced(AOO_CUSTOM_ALLOCATOR)
message(STATUS "Custom allocator support: ${AOO_CUSTOM_ALLOCATOR}")

set(AOO_MAX_PACKET_SIZE 4096 CACHE STRING "max. UDP packet size")
mark_as_advanced(AOO_MAX_PACKET_SIZE)
message(STATUS "Max. UDP packet size: ${AOO_MAX_PACKET_SIZE}")

# compile time debugging options
option(AOO_DEBUG_MEMORY "debug memory usage" OFF)
mark_as_advanced(AOO_DEBUG_MEMORY)

option(AOO_DEBUG_DATA "debug data transmission" OFF)
mark_as_advanced(AOO_DEBUG_DATA)

option(AOO_DEBUG_RESEND "debug data retransmission" OFF)
mark_as_advanced(AOO_DEBUG_RESEND)

option(AOO_DEBUG_DLL "debug time DLL filter" OFF)
mark_as_advanced(AOO_DEBUG_DLL)

option(AOO_DEBUG_RESAMPLER "debug resampler" OFF)
mark_as_advanced(AOO_DEBUG_RESAMPLER)

option(AOO_DEBUG_JITTER_BUFFER "debug jitter buffer" OFF)
mark_as_advanced(AOO_DEBUG_JITTER_BUFFER)

option(AOO_DEBUG_STREAM_MESSAGE "debug jitter buffer" OFF)
mark_as_advanced(AOO_DEBUG_STREAM_MESSAGE)

option(AOO_DEBUG_RELAY "debug relay" OFF)
mark_as_advanced(AOO_DEBUG_RELAY)

option(AOO_DEBUG_CLIENT_MESSAGE "debug client message" OFF)
mark_as_advanced(AOO_DEBUG_CLIENT_MESSAGE)

option(AOO_CLIENT_SIMULATE "network simulation in the client" OFF)
mark_as_advanced(AOO_CLIENT_SIMULATE)

#-----------------------------------------------------------------
# common properties (libaoo, Pd external, examples, tests, etc.)
#-----------------------------------------------------------------

# properties shared by all targets in this repository,
# but which should not be exported to library consumers.
add_library(aoo_common INTERFACE)

# AOO_LOG_LEVEL is also used by the Pd external and some programs
target_compile_definitions(aoo_common INTERFACE AOO_LOG_LEVEL=kAooLogLevel${AOO_LOG_LEVEL})

if (CMAKE_COMPILER_IS_GNUCXX OR CMAKE_COMPILER_IS_CLANG)
    if (AOO_STACK_USAGE)
        target_compile_options(aoo_common INTERFACE -fstack-usage)
    endif()

    # warnings/errors
    target_compile_options(aoo_common INTERFACE -Wall -Wextra)
    # disable some useless and noisy warnings
    target_compile_options(aoo_common INTERFACE
        -Wno-unused-parameter -Wno-unused-variable -Wno-sign-compare -Wno-psabi)
    # some warnings should be errors
    target_compile_options(aoo_common INTERFACE -Werror=return-type)

    # optimization flags
    target_compile_options(aoo_common INTERFACE
        -ffast-math -funroll-loops -fomit-frame-pointer)

    if (HAVE_CXX_SSE)
        target_compile_options(aoo_common INTERFACE -msse)
    endif()

    if (HAVE_CXX_SSE2)
        target_compile_options(aoo_common INTERFACE -msse2)
    endif()

    if (HAVE_CXX_SSE3)
        target_compile_options(aoo_common INTERFACE -msse3)
    endif()

    # people still own old machines that don't support SSE4
    if (FALSE AND HAVE_CXX_SSE4)
        target_compile_options(aoo_common INTERFACE -msse4)
    endif()

    if (HAVE_CXX_FPMATH_SSE)
        target_compile_options(aoo_common INTERFACE -mfpmath=sse)
    endif()

    option(AOO_NATIVE "optimize for this machine (not portable!)" OFF)
    message(STATUS "Optimize for this machine: ${AOO_NATIVE}")
    if (AOO_NATIVE)
        target_compile_options(aoo_common INTERFACE -march=native)
    endif()
endif()

if (MSVC)
    target_compile_options(aoo_common INTERFACE /Zc:__cplusplus /fp:fast)

    # disable some warnings
    # C4267 / C4244: conversion with possible loss of data
    # C4101: unreferenced local variable
    # C4018: signed/unsigned mismatch
    # C4996: unsafe functions (e.g. strcpy() or sscanf())
    target_compile_options(aoo_common INTERFACE
        /wd4267 /wd4244 /wd4101 /wd4018 /wd4996)
endif()

if (MINGW)
    target_compile_options(aoo_common INTERFACE -mstackrealign)
endif()

# common include directories
target_include_directories(aoo_common INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/deps>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/deps/oscpack>)

# clang++:
if (CMAKE_COMPILER_IS_CLANG)
    target_link_options(aoo_common INTERFACE -stdlib=libc++)
endif()

#-------------------------------------------------------------
# common properties for all binaries
#-------------------------------------------------------------

# NOTE: we can't add these link options to aoo_common because
# consumers of the static library would inherit them, although
# they are supposed to be private.
add_library(aoo_binary INTERFACE)

if (AOO_STATIC_RUNTIME)
    # static runtime linker flags
    if (LINUX)
        target_link_options(aoo_binary INTERFACE -static-libstdc++ -static-libgcc)
    elseif (MINGW)
        target_link_options(aoo_binary INTERFACE -static-libstdc++ -static-libgcc -static -lpthread)
    endif()
endif()

if (MINGW)
    # HACK: somehow --strip resp. the install/strip target do not work on MSys2,
    # so we manually strip the symbols at link time - but only in Release mode.
    target_link_options(aoo_binary INTERFACE $<$<CONFIG:RELEASE>:-s>)
endif()

#-----------------------------------------------------------------
# vendored dependencies
#-----------------------------------------------------------------
if (AOO_NET)
    add_subdirectory(deps/md5)
endif()

add_subdirectory(deps/oscpack)

#-----------------------------------------------------------------
# AOO library
#-----------------------------------------------------------------
message(STATUS "")
message(STATUS "*** AOO library ***")
message(STATUS "")
add_subdirectory(aoo)

#-----------------------------------------------------------------
# Pd external
#-----------------------------------------------------------------
if (AOO_BUILD_PD_EXTERNAL)
    message(STATUS "")
    message(STATUS "*** Pure Data external ***")
    message(STATUS "")
    if (NOT AOO_NET)
        message(FATAL_ERROR "Pd external requires AOO_NET")
    endif()
    add_subdirectory(pd)
endif()

#-----------------------------------------------------------------
# SC extension
#-----------------------------------------------------------------
if (AOO_BUILD_SC_EXTENSION)
    if (NOT AOO_NET)
        message(FATAL_ERROR "SuperCollider extension requires AOO_NET")
    endif()
    if (NOT (AOO_SAMPLE_SIZE EQUAL 32))
        message(FATAL_ERROR "SuperCollider extensions requires AOO_SAMPLE_SIZE=32")
    endif()
    message(STATUS "")
    message(STATUS "*** SuperCollider extension ***")
    message(STATUS "")
    add_subdirectory(sc)
endif()

#-----------------------------------------------------------------
# aooserver program
#-----------------------------------------------------------------
if (AOO_BUILD_SERVER)
    message(STATUS "")
    message(STATUS "*** AOO server ***")
    message(STATUS "")
    if (NOT AOO_NET)
        message(FATAL_ERROR "AOO server requires AOO_NET")
    endif()
    add_subdirectory(server)
endif()

#-----------------------------------------------------------------
# examples
#-----------------------------------------------------------------
if (AOO_BUILD_EXAMPLES)
    message(STATUS "")
    message(STATUS "*** examples ***")
    message(STATUS "")
    if (AOO_SAMPLE_SIZE EQUAL 32)
        add_subdirectory(examples/c)
        add_subdirectory(examples/cpp)
    else()
        message(WARNING "Examples require AOO_SAMPLE_SIZE=32 - skipping")
    endif()
endif()

#-----------------------------------------------------------------
# test suite
#-----------------------------------------------------------------
if (AOO_BUILD_TESTS)
    message(STATUS "")
    message(STATUS "*** Test suite ***")
    message(STATUS "")
    add_subdirectory(tests)
endif()

#-----------------------------------------------------------------
# doxygen documentation
#-----------------------------------------------------------------
if (AOO_BUILD_DOCUMENTATION)
    message(STATUS "")
    message(STATUS "*** Documentation ***")
    message(STATUS "")
    add_subdirectory(doxygen)
endif()
