# compile time options
option(CUSTOM_ALLOCATOR "build with custom allocator support" OFF)

option(DYNAMIC_RESAMPLING "enable/disable dynamic resampling" ON)
message(STATUS "use dynamic resampling: ${DYNAMIC_RESAMPLING}")

set(DLL_BANDWIDTH 0.0001 CACHE STRING "default DLL filter bandwidth")
message(STATUS "default DLL filter bandwidth: ${DLL_BANDWIDTH}")

option(XRUN_DETECTION "enable/disable xrun detection" ON)
message(STATUS "xrun detection: ${XRUN_DETECTION}")

set(TIMER_TOLERANCE 0.25 CACHE STRING "default timer check tolerance")
message(STATUS "default timer check tolerance: ${TIMER_TOLERANCE}")

option(BINARY_DATA_MSG "use binary data messages" ON)
message(STATUS "send binary data message: ${BINARY_DATA_MSG}")

# compile time debugging options
option(DEBUG_MEMORY "debug memory usage" OFF)

option(DEBUG_DATA "debug memory usage" OFF)

option(DEBUG_DLL "debug time DLL filter" OFF)

option(DEBUG_TIMER "debug timer" OFF)

option(DEBUG_RESAMPLER "debug resampler" OFF)

option(DEBUG_AUDIO_BUFFER "debug audio buffer" OFF)

option(DEBUG_JITTER_BUFFER "debug jitter buffer" OFF)

set(OPTIONS
    "-DAOO_CUSTOM_ALLOCATOR=$<BOOL:${CUSTOM_ALLOCATOR}>"
    "-DAOO_DYNAMIC_RESAMPLING=$<BOOL:${DYNAMIC_RESAMPLING}>"
    "-DAOO_DLL_BANDWIDTH=${DLL_BANDWIDTH}"
    "-DAOO_XRUN_DETECTION=$<BOOL:${XRUN_DETECTION}>"
    "-DAOO_TIMER_TOLERANCE=${TIMER_TOLERANCE}"
    "-DAOO_BINARY_DATA_MSG=$<BOOL:${BINARY_DATA_MSG}>"
    "-DAOO_DEBUG_MEMORY=$<BOOL:${DEBUG_MEMORY}>"
    "-DAOO_DEBUG_DATA=$<BOOL:${DEBUG_DATA}>"
    "-DAOO_DEBUG_DLL=$<BOOL:${DEBUG_DLL}>"
    "-DAOO_DEBUG_TIMER=$<BOOL:${DEBUG_TIMER}>"
    "-DAOO_DEBUG_RESAMPLER=$<BOOL:${DEBUG_RESAMPLER}>"
    "-DAOO_DEBUG_AUDIO_BUFFER=$<BOOL:${DEBUG_AUDIO_BUFFER}>"
    "-DAOO_DEBUG_JITTER_BUFFER=$<BOOL:${DEBUG_JITTER_BUFFER}>"
)

set(INCLUDE "../include/aoo")
set(COMMON "../common")
set(DEPS "../deps")

file(GLOB HEADERS "${INCLUDE}/*.h" "${INCLUDE}/*.hpp"
     "${INCLUDE}/codec/aoo_pcm.h" "src/*.hpp" "${COMMON}/*.hpp")

file(GLOB SOURCES "src/*.cpp" "src/codec/pcm.cpp" "${COMMON}/*.cpp")

if (NOT SYSTEM_OSCPACK)
    # only build "osc" part
    file(GLOB OSCPACK_HEADERS "${DEPS}/oscpack/osc/*.h")
    file(GLOB OSCPACK_SOURCES "${DEPS}/oscpack/osc/*.cpp")
    list(APPEND HEADERS ${OSCPACK_HEADERS})
    list(APPEND SOURCES ${OSCPACK_SOURCES})
endif()

if (CODEC_OPUS)
    list(APPEND HEADERS "${INCLUDE}/codec/aoo_opus.h")
    list(APPEND SOURCES "src/codec/opus.cpp")
endif()

if (AOO_NET)
    list(APPEND HEADERS "src/net/server.hpp" "src/net/client.hpp"
        "src/net/SLIP.hpp")
    list(APPEND SOURCES "src/net/client.cpp" "src/net/server.cpp")
    if (NOT SYSTEM_MD5)
        list(APPEND HEADERS "${DEPS}/md5/md5.h")
        list(APPEND SOURCES "${DEPS}/md5/md5.c")
    endif()
endif()

# always build static library because we need it for Pd, SC, tests, etc.
# BUILD_AOO_SHARED only installs it
option(BUILD_AOO_SHARED "build 'aoo' shared library" ON)

add_library(${AOO_STATIC} STATIC ${HEADERS} ${SOURCES})
if (NOT MSVC)
    # for MSVC we keep the "aoo_static" name to avoid name clash with
    # "aoo.lib" import library
    set_target_properties(${AOO_STATIC} PROPERTIES OUTPUT_NAME "aoo")
endif()

target_compile_definitions(${AOO_STATIC} PUBLIC ${OPTIONS} "-DAOO_STATIC")
target_link_libraries(${AOO_STATIC} ${OSCPACK_LIB} ${OPUS_LIB})
if (AOO_NET)
    target_link_libraries(${AOO_STATIC} ${MD5_LIB})
endif()

# shared library
option(BUILD_AOO_STATIC "build 'aoo' static library" ON)

if (BUILD_AOO_SHARED)
    add_library(${AOO_SHARED} SHARED ${HEADERS} ${SOURCES})
    set_target_properties(${AOO_SHARED} PROPERTIES OUTPUT_NAME "aoo")

    target_compile_definitions(${AOO_SHARED} PUBLIC "-DAOO_BUILD" ${OPTIONS})
    if (WIN32)
        target_compile_definitions(${AOO_SHARED} PUBLIC "-DDLL_EXPORT")
        target_link_libraries(${AOO_SHARED} "ws2_32")
    endif()
    target_link_libraries(${AOO_SHARED} ${OPUS_LIB} ${LIBS})
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
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/aoo")
endif()
