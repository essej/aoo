# AOO

# compile time options
set(TIMEFILTER_BANDWIDTH 0.0001 CACHE STRING "time filter bandwidth")

set(TIMEFILTER_TOLERANCE 0.25 CACHE STRING "time filter tolerance")

option(TIMEFILTER_CHECK "time filter check" ON)

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

set(AOO_HEADERS "aoo/aoo.h" "aoo/aoo.hpp" "aoo/aoo_pcm.h"
    "aoo/aoo_types.h" "aoo/aoo_utils.hpp" "src/common.hpp"
    "src/lockfree.hpp" "src/sink.hpp" "src/source.hpp"
    "src/sync.hpp" "src/time.hpp" "src/time_dll.hpp")

if (CODEC_OPUS)
    list(APPEND AOO_HEADERS "aoo/aoo_opus.h")
endif()

set(AOO_SRC ${AOO_HEADERS} "src/codec_pcm.cpp" "src/common.cpp"
    "src/sink.cpp" "src/source.cpp" "src/sync.cpp" "src/time.cpp"
    ${OSCPACK_SRC})

if (CODEC_OPUS)
    list(APPEND AOO_SRC "src/codec_opus.cpp")
endif()

add_library(${AOO_STATIC} STATIC ${AOO_SRC})

target_compile_definitions(${AOO_STATIC} PUBLIC ${OPTIONS} "-DAOO_STATIC")

add_library(${AOO} SHARED ${AOO_SRC})

target_compile_definitions(${AOO} PUBLIC "-DAOO_BUILD" ${OPTIONS})
if (WIN32 OR MINGW)
    target_compile_definitions(${AOO} PUBLIC "-DDLL_EXPORT")
endif()
target_link_libraries(${AOO} ${OSCPACK_LIB} ${OPUS_LIB} ${LIBS} )

# AOONET
set(AOONET_HEADERS "aoo/aoo_net.h" "aoo/aoo_types.h"
    "src/client.hpp" "src/lockfree.hpp" "src/net_utils.hpp"
    "src/server.hpp" "src/SLIP.hpp" "src/sync.hpp" "src/time.hpp")

set(AOONET_SRC ${AOONET_HEADERS} ${OSCPACK_SRC} "src/client.cpp"
    "src/net_utils.cpp" "src/server.cpp" "src/sync.cpp" "src/time.cpp")

add_library(${AOONET_STATIC} STATIC ${AOONET_SRC})

target_compile_definitions(${AOONET_STATIC} PUBLIC "-DAOO_STATIC")

add_library(${AOONET} SHARED ${AOONET_SRC})

target_compile_definitions(${AOONET} PUBLIC "-DAOO_BUILD")
target_link_libraries(${AOONET} ${MD5_LIB} ${OSCPACK_LIB} ${LIBS})
if (WIN32 OR MINGW)
    target_compile_definitions(${AOONET} PUBLIC "-DDLL_EXPORT")
    target_link_libraries(${AOONET} "ws2_32")
endif()
