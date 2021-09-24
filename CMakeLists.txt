cmake_minimum_required (VERSION 3.1)

set(PROJECT "aoo")
message(STATUS "Project: ${PROJECT}")
project(${PROJECT})

include(GNUInstallDirs)
include (CheckCCompilerFlag)
include (CheckCXXCompilerFlag)

if(UNIX AND NOT APPLE AND NOT MINGW)
	set(LINUX TRUE)
endif()

# some MinGW setups don't define WIN32!
if (MINGW AND NOT WIN32)
    message(WARNING "Your MinGW setup doesn't define WIN32")
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
	option(STATIC_LIBS "link with static libraries (libstdc++ and libgcc)" ON)
endif()
if(MINGW)
	option(STATIC_LIBS "link with static libraries (libstdc++, libgcc and phread)" ON)
	set(CMAKE_EXECUTABLE_SUFFIX ".exe")
endif()

# logging
set(LOG_LEVEL "Warning" CACHE STRING "LOG_LEVEL")
message(STATUS "LOG_LEVEL: ${LOG_LEVEL}")
add_definitions("-DAOO_LOG_LEVEL=kAooLogLevel${LOG_LEVEL}")

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
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")

    if(CMAKE_COMPILER_IS_CLANG)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
    endif()
endif()
if (MINGW)
    # set(CMAKE_CXX_COMPILER g++)
    add_compile_options("-mstackrealign")
endif()
if (LINUX)
    add_compile_options("-fPIC")
endif()

# custom feature tests
set(AOO_CMAKE "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

function(test_atomics feature file)
    message(STATUS "Checking ${feature} support")

    # For now we just try if it compiles and links.
    # If C++17 is enabled, we would also get a compilation
    # error if the atomics are not always lockfree.
    # Also, the executable return EXIT_FAILURE if is_lock_free()
    # is false, so we could actually do a runtime test,
    # but is doesn't work when cross compiling....
    # LATER find a better solution.
    try_compile(RESULT_VAR
        "${AOO_CMAKE}/build" "${AOO_CMAKE}/${file}"
        OUTPUT_VARIABLE COMPILE_OUTPUT)

    if (RESULT_VAR)
        message(STATUS "- ok")
        add_definitions("-DHAVE_${feature}")
    else()
        message(STATUS "- failed")
        message(${COMPILE_OUTPUT})
        message("Falling back to software emulation")
    endif()
endfunction()

# atomic doubles
test_atomics("ATOMIC_DOUBLE" "atomic_double.cpp")
# atomic 64-bit integers
test_atomics("ATOMIC_INT64" "atomic_int64.cpp")

# networking support
option(AOO_NET "build with networking support" ON)
add_definitions("-DUSE_AOO_NET=$<BOOL:${AOO_NET}>")

# dependencies
include_directories(deps)

# oscpack
option(SYSTEM_OSCPACK "use system provided oscpack library" OFF)
if (SYSTEM_OSCPACK)
    set(OSCPACK_LIB "oscpack" CACHE STRING "oscpack linker flags")
endif()

# md5
option(SYSTEM_MD5 "use system provided md5 library" OFF)
if (SYSTEM_MD5)
    set(MD5_LIB "md5" CACHE STRING "md5 linker flags")
endif()

# Opus
option(CODEC_OPUS "use Opus codec" ON)
if (CODEC_OPUS)
    add_definitions("-DUSE_CODEC_OPUS=1")

    option(SYSTEM_OPUS "use system provided Opus library" OFF)
    if (SYSTEM_OPUS)
        set(OPUS_LIB "opus" CACHE STRING "Opus linker flags")
    else()
        include_directories("deps/opus/include")
        # use different variable for finding the library,
        # because find_library() can't override a cache variable.
        find_library(OPUS_LIB_LOCAL "opus" HINTS "deps/opus/lib" NO_DEFAULT_PATH)
        set(OPUS_LIB ${OPUS_LIB_LOCAL})
        if (MINGW)
            list(APPEND LIBS "ssp") # for fortified functions
        endif()
    endif()
    message(STATUS "Opus library: ${OPUS_LIB}")
endif()

# platform specific linker flags
if (LINUX)
    list(APPEND LIBS "-pthread")
    if(STATIC_LIBS)
        list(APPEND LIBS "-static-libstdc++" "-static-libgcc")
    endif()
    set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)
endif()
if (MINGW)
    if (STATIC_LIBS)
        list(APPEND LIBS "-static-libstdc++" "-static-libgcc" "-static -lpthread")
    else()
        list(APPEND LIBS "-lpthread")
    endif()
endif()
if (APPLE)
    list(APPEND LIBS "-lpthread")
endif()

# headers
include_directories(.)
include_directories(include)

# "aoo" library
set(AOO_SHARED "aoo_shared")
set(AOO_STATIC "aoo_static")
add_subdirectory(aoo)

# Pd external
option(BUILD_PD_EXTERNAL "build Pd external" ON)
if (BUILD_PD_EXTERNAL)
    if (NOT AOO_NET)
        message(FATAL_ERROR "Pd external requires AOO_NET")
    endif()
    add_subdirectory(pd)
endif()

# SC extension
option(BUILD_SC_EXTENSION "build SC extension" ON)
if (BUILD_SC_EXTENSION)
    if (NOT AOO_NET)
        message(FATAL_ERROR "SC extension requires AOO_NET")
    endif()
    add_subdirectory(sc)
endif()


# test suite
option(BUILD_TEST_SUITE "build test suite" ON)
if (BUILD_TEST_SUITE)
    add_subdirectory(tests)
endif()
