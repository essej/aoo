cmake_minimum_required (VERSION 3.1)

message(STATUS "Project: aoo")
project(aoo)

include(GNUInstallDirs)
include (CheckCCompilerFlag)
include (CheckCXXCompilerFlag)

# built everything (including Opus) with -fPIC
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# dependencies
include_directories(deps)

# Opus
# NOTE: build it first, so we don't pollute it with our own settings
option(AOO_CODEC_OPUS "use Opus codec" ON)
if (AOO_CODEC_OPUS)
    option(AOO_SYSTEM_OPUS "use system provided Opus library" ON)
    if (AOO_SYSTEM_OPUS)
        set(AOO_OPUS_LDFLAGS "opus" CACHE STRING "Opus linker flags")
        message(STATUS "Opus library: ${AOO_OPUS_LDFLAGS}")
    else()
        message(STATUS "\n*** Opus library ***\n")

        add_subdirectory("deps/opus" EXCLUDE_FROM_ALL)

        # HACK to force local opus lib (needed for MSVC)
        add_library(opus_local ALIAS opus)

        set(AOO_OPUS_LDFLAGS opus_local)
    endif()
endif()

message(STATUS "\n*** Global settings ***\n")

# oscpack
option(AOO_SYSTEM_OSCPACK "use system provided oscpack library" OFF)
if (AOO_SYSTEM_OSCPACK)
	set(AOO_OSCPACK_LDFLAGS "oscpack" CACHE STRING "oscpack linker flags")
endif()

# md5
option(AOO_SYSTEM_MD5 "use system provided md5 library" OFF)
if (AOO_SYSTEM_MD5)
	set(AOO_MD5_LDFLAGS "md5" CACHE STRING "md5 linker flags")
endif()

if(UNIX AND NOT APPLE AND NOT MINGW)
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

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

message(STATUS "CMAKE_INSTALL_PREFIX: ${CMAKE_INSTALL_PREFIX}")

if(LINUX AND CMAKE_COMPILER_IS_GNUCXX)
	option(AOO_STATIC_LIBS "link with static libraries (libstdc++ and libgcc)" ON)
endif()
if(MINGW)
	option(AOO_STATIC_LIBS "link with static libraries (libstdc++, libgcc and phread)" ON)
	set(CMAKE_EXECUTABLE_SUFFIX ".exe")
endif()

# logging
set(AOO_LOG_LEVEL "Warning" CACHE STRING "compile time log level")
message(STATUS "Log level: ${AOO_LOG_LEVEL}")
add_definitions(-DAOO_LOG_LEVEL=kAooLogLevel${AOO_LOG_LEVEL})

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

# compiler flags
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (CMAKE_COMPILER_IS_GNUCXX OR CMAKE_COMPILER_IS_CLANG)
    add_compile_options("-fvisibility=hidden")

    CHECK_CXX_COMPILER_FLAG("-msse" HAS_CXX_SSE)
    if (HAS_CXX_SSE)
        add_compile_options("-msse")
    endif()

    CHECK_CXX_COMPILER_FLAG("-msse2" HAS_CXX_SSE2)
    if (HAS_CXX_SSE2)
        add_compile_options("-msse2")
    endif()

    CHECK_CXX_COMPILER_FLAG("-msse3" HAS_CXX_SSE3)
    if (HAS_CXX_SSE3)
        add_compile_options("-msse3")
    endif()

    # people still own old machines that don't support SSE4
    if (FALSE)
        CHECK_CXX_COMPILER_FLAG("-msse4" HAS_CXX_SSE4)
        if (HAS_CXX_SSE4)
            add_compile_options("-msse4")
        endif()
    endif()

    CHECK_CXX_COMPILER_FLAG("-mfpmath=sse" HAS_CXX_FPMATH_SSE)
    if (HAS_CXX_FPMATH_SSE)
        add_compile_options("-mfpmath=sse")
    endif()

    if(NATIVE)
        add_compile_options("-march=native")
    endif()

    add_compile_options("-ffast-math" "-funroll-loops" "-fomit-frame-pointer")

    # C++ only:
    if(CMAKE_COMPILER_IS_CLANG)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
    endif()
endif()

if (MINGW)
    # set(CMAKE_CXX_COMPILER g++)
    add_compile_options("-mstackrealign")
endif()

# custom feature tests
set(CMAKEDIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

function(test_atomics feature file)
    if (DEFINED AOO_HAVE_${feature})
        return()
    endif()

    message(STATUS "Checking ${feature} support")

    # For now we just try if it compiles and links.
    # If C++17 is available, we also get a compilation error
    # if the atomics are not always lockfree; otherwise
    # we automatically fall back to C++14.
    # It would also be possible to do a runtime test with,
    # is_lock_free(), but it wouldn't work for cross compiling...
    try_compile(RESULT_VAR
        "${CMAKEDIR}/build" "${CMAKEDIR}/${file}"
        OUTPUT_VARIABLE COMPILE_OUTPUT
        CXX_STANDARD 17)

    if (RESULT_VAR)
        message(STATUS "- ok")
        add_definitions(-DHAVE_${feature})
    else()
        message(STATUS "- failed")
        message(${COMPILE_OUTPUT})
        message("Falling back to software emulation")
    endif()

    set(AOO_HAVE_${feature} ${RESULT_VAR} CACHE BOOL "${feature} support")
endfunction()

# atomic doubles
test_atomics(ATOMIC_DOUBLE "atomic_double.cpp")
# message(STATUS "atomic doubles supported: ${AOO_HAVE_ATOMIC_DOUBLE}")

# atomic 64-bit integers
test_atomics(ATOMIC_INT64 "atomic_int64.cpp")
# message(STATUS "atomic 64-bit integers supported: ${AOO_HAVE_ATOMIC_INT64}")

# opus support
message(STATUS "Use Opus codec: ${AOO_CODEC_OPUS}")
add_definitions(-DUSE_CODEC_OPUS=$<BOOL:${AOO_CODEC_OPUS}>)
if (AOO_CODEC_OPUS AND (NOT AOO_SYSTEM_OPUS))
    # because of a quirk in the opus source code structure,
    # we can't include "opus/opus_multistream.h"
    add_definitions(-DAOO_OPUS_MULTISTREAM_H=<opus/include/opus_multistream.h>)
endif()

# networking support
option(AOO_NET "build with networking support" ON)
message(STATUS "Use AOO NET: ${AOO_NET}")
add_definitions(-DUSE_AOO_NET=$<BOOL:${AOO_NET}>)

# platform specific linker flags
if (LINUX)
    list(APPEND LIBS "-pthread")
    if(AOO_STATIC_LIBS)
        list(APPEND LIBS "-static-libstdc++" "-static-libgcc")
    endif()
    set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)
endif()
if (MINGW)
    if (AOO_STATIC_LIBS)
        list(APPEND LIBS "-static-libstdc++" "-static-libgcc" "-static -lpthread")
    else()
        list(APPEND LIBS "-lpthread")
    endif()
    if (AOO_CODEC_OPUS)
        list(APPEND LIBS "ssp") # for fortified functions
    endif()
endif()
if (APPLE)
    list(APPEND LIBS "-lpthread")
endif()

# headers
include_directories(".")
include_directories("include")

# AOO library
message(STATUS "\n*** AOO library ***\n")
add_subdirectory("aoo")

# Pd external
option(AOO_BUILD_PD_EXTERNAL "build Pd external" ON)
if (AOO_BUILD_PD_EXTERNAL)
    if (NOT AOO_NET)
        message(FATAL_ERROR "Pd external requires AOO_NET")
    endif()
    message(STATUS "\n*** Pd external ***\n")
    add_subdirectory("pd")
endif()

# SC extension
option(AOO_BUILD_SC_EXTENSION "build SC extension" ON)
if (AOO_BUILD_SC_EXTENSION)
    if (NOT AOO_NET)
        message(FATAL_ERROR "SC extension requires AOO_NET")
    endif()
    message(STATUS "\n*** SC extension ***\n")
    add_subdirectory("sc")
endif()


# test suite
option(AOO_BUILD_TEST_SUITE "build test suite" ON)
if (AOO_BUILD_TEST_SUITE)
    message(STATUS "\n*** Test suite ***\n")
    add_subdirectory("tests")
endif()
