cmake_minimum_required (VERSION 2.8)

# compile time options
set(TIMEFILTER_BANDWIDTH 0.0001 CACHE STRING "default time filter bandwidth")
message(STATUS "default time filter bandwidth: ${TIMEFILTER_BANDWIDTH}")

set(TIMEFILTER_TOLERANCE 0.25 CACHE STRING "default time filter tolerance")
message(STATUS "default time filter tolerance: ${TIMEFILTER_TOLERANCE}")

option(TIMEFILTER_CHECK "use time filter check" ON)
message(STATUS "time filter check: ${TIMEFILTER_CHECK}")

# compile time debugging options
option(DEBUG_TIMEFILTER "debug time filter" OFF)

option(DEBUG_DLL "debug delay locked loop" OFF)

option(DEBUG_RESAMPLING "debug resampling" OFF)

option(DEBUG_AUDIO_BUFFER "debug audio buffer" OFF)

option(DEBUG_JITTER_BUFFER "debug jitter buffer" OFF)

set(OPTIONS
    "-DAOO_TIMEFILTER_BANDWIDTH=${TIMEFILTER_BANDWIDTH}"
    "-DAOO_TIMEFILTER_TOLERANCE=${TIMEFILTER_TOLERANCE}"
    "-DAOO_TIMEFILTER_CHECK=$<BOOL:${TIMEFILTER_CHECK}>"
    "-DAOO_DEBUG_TIMEFILTER=$<BOOL:${DEBUG_TIMEFILTER}>"
    "-DAOO_DEBUG_DLL=$<BOOL:${DEBUG_DLL}>"
    "-DAOO_DEBUG_RESAMPLING=$<BOOL:${DEBUG_RESAMPLING}>"
    "-DAOO_DEBUG_AUDIO_BUFFER=$<BOOL:${DEBUG_AUDIO_BUFFER}>"
    "-DAOO_DEBUG_JITTER_BUFFER=$<BOOL:${DEBUG_JITTER_BUFFER}>"
)

set(INCLUDE "../include/aoo")
set(COMMON "../common")

file(GLOB AOO_HEADERS "${INCLUDE}/*.h" "${INCLUDE}/*.hpp"
     "${INCLUDE}/codec/aoo_pcm.h" "src/*.hpp"
     "${COMMON}/lockfree.hpp" "${COMMON}/sync.hpp"
     "${COMMON}/time.hpp" "${COMMON}/utils.hpp")
if (CODEC_OPUS)
    list(APPEND AOO_HEADERS "${INCLUDE}/codec/aoo_opus.h")
endif()

file(GLOB AOO_SRC ${AOO_HEADERS} ${AOO_COMMON_SRC}
     ${OSCPACK_SRC} "src/*.cpp" "src/codec/pcm.cpp"
     "${COMMON}/sync.cpp" "${COMMON}/time.cpp")
if (CODEC_OPUS)
    list(APPEND AOO_SRC "src/codec/opus.cpp")
endif()

# static library
add_library(${AOO_STATIC} STATIC ${AOO_SRC})

target_compile_definitions(${AOO_STATIC} PUBLIC ${OPTIONS} "-DAOO_STATIC")

# shared library
option(BUILD_AOO_SHARED "build 'aoo' as shared library" ON)

if (BUILD_AOO_SHARED)
    add_library(${AOO_SHARED} SHARED ${AOO_SRC})

    target_compile_definitions(${AOO_SHARED} PUBLIC "-DAOO_BUILD" ${OPTIONS})
    if (WIN32 OR MINGW)
        target_compile_definitions(${AOO_SHARED} PUBLIC "-DDLL_EXPORT")
    endif()
    target_link_libraries(${AOO_SHARED} ${OSCPACK_LIB} ${CODEC_OPUS_LIB} ${LIBS})

    # install shared libraries
    install(TARGETS ${AOO_SHARED}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    # install public headers (note the trailing slash!)
    install(DIRECTORY "${INCLUDE}/"
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${AOO_SHARED})
endif()
