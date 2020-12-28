cmake_minimum_required (VERSION 2.8)

# networking support
option(USE_AOO_NET "build with networking support" ON)

# compile time options
option(CUSTOM_ALLOCATOR "build with custom allocator support" OFF)

set(TIMEFILTER_BANDWIDTH 0.0001 CACHE STRING "default time filter bandwidth")
message(STATUS "default time filter bandwidth: ${TIMEFILTER_BANDWIDTH}")

set(TIMEFILTER_TOLERANCE 0.25 CACHE STRING "default time filter tolerance")
message(STATUS "default time filter tolerance: ${TIMEFILTER_TOLERANCE}")

option(TIMEFILTER_CHECK "use time filter check" ON)
message(STATUS "time filter check: ${TIMEFILTER_CHECK}")

# compile time debugging options
option(DEBUG_MEMORY "debug memory usage" OFF)

option(DEBUG_TIMEFILTER "debug time filter" OFF)

option(DEBUG_DLL "debug delay locked loop" OFF)

option(DEBUG_RESAMPLING "debug resampling" OFF)

option(DEBUG_AUDIO_BUFFER "debug audio buffer" OFF)

option(DEBUG_JITTER_BUFFER "debug jitter buffer" OFF)

set(OPTIONS
    "-DAOO_CUSTOM_ALLOCATOR=$<BOOL:${CUSTOM_ALLOCATOR}>"
    "-DAOO_TIMEFILTER_BANDWIDTH=${TIMEFILTER_BANDWIDTH}"
    "-DAOO_TIMEFILTER_TOLERANCE=${TIMEFILTER_TOLERANCE}"
    "-DAOO_TIMEFILTER_CHECK=$<BOOL:${TIMEFILTER_CHECK}>"
    "-DAOO_DEBUG_MEMORY=$<BOOL:${DEBUG_MEMORY}>"
    "-DAOO_DEBUG_TIMEFILTER=$<BOOL:${DEBUG_TIMEFILTER}>"
    "-DAOO_DEBUG_DLL=$<BOOL:${DEBUG_DLL}>"
    "-DAOO_DEBUG_RESAMPLING=$<BOOL:${DEBUG_RESAMPLING}>"
    "-DAOO_DEBUG_AUDIO_BUFFER=$<BOOL:${DEBUG_AUDIO_BUFFER}>"
    "-DAOO_DEBUG_JITTER_BUFFER=$<BOOL:${DEBUG_JITTER_BUFFER}>"
)

set(INCLUDE "../include/aoo")
set(COMMON "../common")

file(GLOB AOO_HEADERS "${INCLUDE}/*.h" "${INCLUDE}/*.hpp"
     "${INCLUDE}/codec/aoo_pcm.h" "src/*.hpp" "${COMMON}/*.hpp")
file(GLOB AOO_SRC "src/*.cpp" "src/codec/pcm.cpp" "${COMMON}/*.cpp")

if (CODEC_OPUS)
    list(APPEND AOO_HEADERS "${INCLUDE}/codec/aoo_opus.h")
    list(APPEND AOO_SRC "src/codec/opus.cpp")
endif()

if (USE_AOO_NET)
    list(APPEND AOO_HEADERS "src/net/server.hpp" "src/net/client.hpp"
        "src/net/SLIP.hpp")
    list(APPEND AOO_SRC "src/net/client.cpp" "src/net/server.cpp")
endif()

# static library
add_library(${AOO_STATIC} STATIC ${AOO_HEADERS} ${AOO_SRC})

target_compile_definitions(${AOO_STATIC} PUBLIC ${OPTIONS} "-DAOO_STATIC")

# shared library
option(BUILD_AOO_SHARED "build 'aoo' shared library" ON)
option(BUILD_AOO_STATIC "build 'aoo' static library" ON)

if (BUILD_AOO_SHARED)
    add_library(${AOO_SHARED} SHARED ${AOO_HEADERS} ${AOO_SRC})

    target_compile_definitions(${AOO_SHARED} PUBLIC "-DAOO_BUILD" ${OPTIONS})
    if (WIN32 OR MINGW)
        target_compile_definitions(${AOO_SHARED} PUBLIC "-DDLL_EXPORT")
        target_link_libraries(${AOO_SHARED} "ws2_32")
    endif()
    if (USE_AOO_NET)
        target_link_libraries(${AOO_SHARED} ${MD5_LIB})
    endif()
    target_link_libraries(${AOO_SHARED} ${OSCPACK_LIB} ${CODEC_OPUS_LIB} ${LIBS})   
endif()

if (BUILD_AOO_SHARED OR BUILD_AOO_STATIC)
    # install library files
    if (BUILD_AOO_SHARED)
        list(APPEND TARGET_LIST ${AOO_SHARED})
    endif()
    if (BUILD_AOO_STATIC)
        list(APPEND TARGET_LIST ${AOO_STATIC})
    endif()
    install(TARGETS ${TARGET_LIST}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    # install public headers (note the trailing slash!)
    install(DIRECTORY "${INCLUDE}/"
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${AOO_SHARED})
endif()
