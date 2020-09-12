# AOO

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

set(AOO_PUBLIC_HEADERS "aoo/aoo.h" "aoo/aoo.hpp"
    "aoo/aoo_types.h" "aoo/aoo_pcm.h" )
if (CODEC_OPUS)
    list(APPEND AOO_PUBLIC_HEADERS "aoo/aoo_opus.h")
endif()


set(AOO_HEADERS "aoo/aoo_utils.hpp" "src/common.hpp"
    "src/lockfree.hpp" "src/sink.hpp" "src/source.hpp"
    "src/sync.hpp" "src/time.hpp" "src/time_dll.hpp")

set(AOO_SRC ${AOO_PUBLIC_HEADERS} ${AOO_HEADERS} ${OSCPACK_SRC}
    "src/codec_pcm.cpp" "src/common.cpp" "src/sink.cpp"
    "src/source.cpp" "src/sync.cpp" "src/time.cpp")

if (CODEC_OPUS)
    list(APPEND AOO_SRC "src/codec_opus.cpp")
endif()

# static library
add_library(${AOO_STATIC} STATIC ${AOO_SRC})

target_compile_definitions(${AOO_STATIC} PUBLIC ${OPTIONS} "-DAOO_STATIC")

# shared library
if (BUILD_AOO_SHARED)
    add_library(${AOO_SHARED} SHARED ${AOO_SRC})

    target_compile_definitions(${AOO_SHARED} PUBLIC "-DAOO_BUILD" ${OPTIONS})
    if (WIN32 OR MINGW)
        target_compile_definitions(${AOO_SHARED} PUBLIC "-DDLL_EXPORT")
    endif()
    target_link_libraries(${AOO_SHARED} ${OSCPACK_LIB} ${CODEC_OPUS_LIB} ${LIBS} )

    set_target_properties(${AOO_SHARED} PROPERTIES
        PUBLIC_HEADER "${AOO_PUBLIC_HEADERS}")

    install(TARGETS ${AOO_SHARED}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${AOO_SHARED})
endif()

# AOONET
set(AOONET_PUBLIC_HEADERS "aoo/aoo_net.h" "aoo/aoo_net.hpp")

set(AOONET_HEADERS "src/client.hpp" "src/lockfree.hpp" "src/net_utils.hpp"
    "src/server.hpp" "src/SLIP.hpp" "src/sync.hpp" "src/time.hpp")

set(AOONET_SRC ${AOO_PUBLIC_HEADERS} ${AOONET_HEADERS} ${OSCPACK_SRC}
    "src/client.cpp" "src/net_utils.cpp" "src/server.cpp" "src/sync.cpp" "src/time.cpp")

# static library
add_library(${AOONET_STATIC} STATIC ${AOONET_SRC})

target_compile_definitions(${AOONET_STATIC} PUBLIC "-DAOO_STATIC")

# shared library
if (BUILD_AOONET_SHARED)
    add_library(${AOONET_SHARED} SHARED ${AOONET_SRC})

    target_compile_definitions(${AOONET_SHARED} PUBLIC "-DAOO_BUILD")
    target_link_libraries(${AOONET_SHARED} ${MD5_LIB} ${OSCPACK_LIB} ${LIBS})
    if (WIN32 OR MINGW)
        target_compile_definitions(${AOONET_SHARED} PUBLIC "-DDLL_EXPORT")
        target_link_libraries(${AOONET_SHARED} "ws2_32")
    endif()

    set_target_properties(${AOONET_SHARED} PROPERTIES
        PUBLIC_HEADER "${AOONET_PUBLIC_HEADERS}")

    install(TARGETS ${AOONET_SHARED}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${AOONET_SHARED})
endif()
