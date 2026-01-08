# AOO library

add_library(aoo
    # public headers
    "../include/aoo.h"
    "../include/aoo_client.h"
    "../include/aoo_client.hpp"
    "../include/aoo_codec.h"
    "../include/aoo_config.h"
    "../include/aoo_controls.h"
    "../include/aoo_defines.h"
    "../include/aoo_events.h"
    "../include/aoo_requests.h"
    "../include/aoo_server.h"
    "../include/aoo_server.hpp"
    "../include/aoo_sink.h"
    "../include/aoo_sink.hpp"
    "../include/aoo_source.h"
    "../include/aoo_source.hpp"
    "../include/aoo_types.h"
    "../include/codec/aoo_null.h"
    "../include/codec/aoo_pcm.h"
    # sources
    "src/aoo.cpp"
    "src/binmsg.hpp"
    "src/data_frame.cpp"
    "src/data_frame.hpp"
    "src/detail.hpp"
    "src/events.hpp"
    "src/memory.hpp"
    "src/packet_buffer.cpp"
    "src/packet_buffer.hpp"
    "src/resampler.cpp"
    "src/resampler.hpp"
    "src/rt_memory_pool.hpp"
    "src/sink.cpp"
    "src/sink.hpp"
    "src/source.cpp"
    "src/source.hpp"
    "src/time_dll.hpp"
    "src/codec/null.cpp"
    "src/codec/pcm.cpp"
    # common sources
    "../common/bit_utils.hpp"
    "../common/copyable_atomic.hpp"
    "../common/lockfree.hpp"
    "../common/log.hpp"
    "../common/net_utils.cpp"
    "../common/net_utils.hpp"
    "../common/priority_queue.hpp"
    "../common/sync.cpp"
    "../common/sync.hpp"
    "../common/tagged_integer.hpp"
    "../common/time.cpp"
    "../common/time.hpp"
    "../common/utils.hpp"
    # oscpack
    $<TARGET_OBJECTS:aoo_oscpack>)

target_include_directories(aoo PRIVATE ".")

if (AOO_NET)
    target_sources(aoo PRIVATE
        "src/net/client.cpp"
        "src/net/client.hpp"
        "src/net/client_endpoint.cpp"
        "src/net/client_endpoint.hpp"
        "src/net/client_events.hpp"
        "src/net/detail.cpp"
        "src/net/detail.hpp"
        "src/net/event.hpp"
        "src/net/message_buffer.cpp"
        "src/net/message_buffer.hpp"
        "src/net/osc_stream_receiver.hpp"
        "src/net/peer.cpp"
        "src/net/peer.hpp"
        "src/net/ping_timer.hpp"
        "src/net/server.cpp"
        "src/net/server.hpp"
        "src/net/server_events.hpp"
        "src/net/simulate.hpp"
        "src/net/tcp_server.cpp"
        "src/net/tcp_server.hpp"
        "src/net/udp_server.cpp"
        "src/net/udp_server.hpp")

    target_sources(aoo PRIVATE $<TARGET_OBJECTS:aoo_md5>)
endif()

if (AOO_USE_OPUS)
    target_sources(aoo PRIVATE
        "../include/codec/aoo_opus.h"
        "src/codec/opus.cpp")
endif()

target_compile_definitions(aoo PRIVATE
    AOO_BUILD
    # compile time options
    AOO_CUSTOM_ALLOCATOR=$<BOOL:${AOO_CUSTOM_ALLOCATOR}>
    AOO_MAX_PACKET_SIZE=${AOO_MAX_PACKET_SIZE}
    AOO_DEBUG_MEMORY=$<BOOL:${AOO_DEBUG_MEMORY}>
    AOO_DEBUG_DATA=$<BOOL:${AOO_DEBUG_DATA}>
    AOO_DEBUG_RESEND=$<BOOL:${AOO_DEBUG_RESEND}>
    AOO_DEBUG_DLL=$<BOOL:${AOO_DEBUG_DLL}>
    AOO_DEBUG_RESAMPLER=$<BOOL:${AOO_DEBUG_RESAMPLER}>
    AOO_DEBUG_STREAM_MESSAGE=$<BOOL:${AOO_DEBUG_STREAM_MESSAGE}>
    AOO_DEBUG_JITTER_BUFFER=$<BOOL:${AOO_DEBUG_JITTER_BUFFER}>
    AOO_DEBUG_RELAY=$<BOOL:${AOO_DEBUG_RELAY}>
    AOO_DEBUG_CLIENT_MESSAGE=$<BOOL:${AOO_DEBUG_CLIENT_MESSAGE}>
    AOO_CLIENT_SIMULATE=$<BOOL:${AOO_CLIENT_SIMULATE}>
    # features
    $<$<BOOL:${AOO_HAVE_ATOMIC_DOUBLE}>:AOO_HAVE_ATOMIC_DOUBLE>
    $<$<BOOL:${AOO_HAVE_ATOMIC_INT64}>:AOO_HAVE_ATOMIC_INT64>
    $<$<BOOL:${AOO_HAVE_PTHREAD_RWLOCK}>:AOO_HAVE_PTHREAD_RWLOCK>
)

target_link_libraries(aoo PRIVATE aoo_common)
if (BUILD_SHARED_LIBS)
    target_link_libraries(aoo PRIVATE aoo_binary)
endif()

# link with "libatomic" if available and necessary
if (AOO_HAVE_LIB_ATOMIC AND
        (NOT AOO_HAVE_ATOMIC_DOUBLE OR NOT AOO_HAVE_ATOMIC_INT64))
    target_link_options(aoo PRIVATE -latomic)
endif()

set_target_properties(aoo PROPERTIES
    SOVERSION ${aoo_version_major}
    VERSION ${aoo_version})

# static library
if (NOT BUILD_SHARED_LIBS AND MSVC)
    # for MSVC we add a "_static" suffix to avoid a name clash
    # with the "aoo.lib" import library
    set_target_properties(aoo PROPERTIES OUTPUT_NAME "aoo_static")
endif()

add_library(Aoo::aoo ALIAS aoo)

#-------------------------------------------------------------
# public properties (for libaoo consumers)
#-------------------------------------------------------------

target_compile_definitions(aoo PUBLIC
    $<$<BOOL:${BUILD_SHARED_LIBS}>:AOO_SHARED>
    AOO_NET=$<BOOL:${AOO_NET}>
    AOO_USE_IPV6=$<BOOL:${AOO_USE_IPV6}>
    AOO_USE_OPUS=$<BOOL:${AOO_USE_OPUS}>
    AOO_SAMPLE_SIZE=${AOO_SAMPLE_SIZE})

target_include_directories(aoo PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/aoo>)

if (LINUX)
    target_compile_options(aoo_common INTERFACE -pthread)
    target_link_libraries(aoo PRIVATE -pthread)
endif()

if (MINGW)
    if (NOT AOO_STATIC_RUNTIME)
        target_link_libraries(aoo PRIVATE pthread)
    endif()
    if (AOO_USE_OPUS)
        target_link_libraries(aoo PRIVATE ssp) # for fortified functions
    endif()
endif()

if (APPLE)
    target_link_libraries(aoo PRIVATE pthread)
endif()

if (WIN32)
    target_link_libraries(aoo PRIVATE ws2_32)
endif()

if (AOO_USE_OPUS)
    target_link_libraries(aoo PUBLIC Opus::opus)
endif()

#-------------------------------------------------------------
# AOO installation
#-------------------------------------------------------------

if (NOT AOO_INSTALL_LIBRARY)
    return()
endif()

# install library files
install(TARGETS aoo
    EXPORT AooTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            COMPONENT Aoo_Development
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            COMPONENT Aoo_Runtime
            NAMELINK_COMPONENT Aoo_Development
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            COMPONENT Aoo_Runtime
)

if (NOT BUILD_SHARED_LIBS)
    # for some reason the static library has aoo_common
    # as a dependency, even though the latter does not
    # contain any link options...
    install(TARGETS aoo_common EXPORT AooTargets)
endif()

# TODO: install PDB file (Visual Studio)

# install public headers
install(FILES
    "../include/aoo.h"
    "../include/aoo_codec.h"
    "../include/aoo_config.h"
    "../include/aoo_controls.h"
    "../include/aoo_defines.h"
    "../include/aoo_events.h"
    "../include/aoo_sink.h"
    "../include/aoo_sink.hpp"
    "../include/aoo_source.h"
    "../include/aoo_source.hpp"
    "../include/aoo_types.h"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/aoo"
    COMPONENT Aoo_Development
)

# AOO_NET headers
if (AOO_NET)
    install(FILES
        "../include/aoo_client.h"
        "../include/aoo_client.hpp"
        "../include/aoo_requests.h"
        "../include/aoo_server.h"
        "../include/aoo_server.hpp"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/aoo"
        COMPONENT Aoo_Development
    )
endif()

# codec headers
install(FILES
    "../include/codec/aoo_null.h"
    "../include/codec/aoo_pcm.h"
    $<$<BOOL:${AOO_USE_OPUS}>:${CMAKE_CURRENT_SOURCE_DIR}/../include/codec/aoo_opus.h>
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/aoo/codec"
    COMPONENT Aoo_Development
)

# README and LICENSE
install(FILES
    "../README.md" "../INSTALL.md" "../LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/aoo"
)

# documentation
install(FILES
    "../doc/aoo_protocol.md"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)

#-------------------------------------------------------------
# CMake package module
#-------------------------------------------------------------
if (AOO_INSTALL_CMAKE_CONFIG_MODULE)
    include (CMakePackageConfigHelpers)

    set(AOO_INSTALL_CMAKE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/aoo"
        CACHE STRING "Path to AOO CMake files")
    mark_as_advanced(AOO_INSTALL_CMAKE_DIR)

    if (BUILD_SHARED_LIBS)
        set(type "Shared")
    else()
        set(type "Static")
    endif()

    install(EXPORT AooTargets
            DESTINATION "${AOO_INSTALL_CMAKE_DIR}"
            NAMESPACE Aoo::
            FILE "Aoo${type}Targets.cmake"
            COMPONENT Aoo_Development)

    configure_package_config_file("../cmake/AooConfig.cmake.in"
        "AooConfig.cmake"
        INSTALL_DESTINATION "${AOO_INSTALL_CMAKE_DIR}")

    write_basic_package_version_file("AooConfigVersion.cmake"
        VERSION "${aoo_version}"
        COMPATIBILITY SameMajorVersion)

    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/AooConfig.cmake"
                  "${CMAKE_CURRENT_BINARY_DIR}/AooConfigVersion.cmake"
            DESTINATION "${AOO_INSTALL_CMAKE_DIR}")

    if (AOO_USE_OPUS)
        # needed for AooConfig.cmake so find_package(Opus) works in downstream projects
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/FindOpus.cmake"
                DESTINATION "${AOO_INSTALL_CMAKE_DIR}")
    endif()
endif()

#-------------------------------------------------------------
# pkg-config module
#-------------------------------------------------------------
if (AOO_INSTALL_PKG_CONFIG_MODULE)
    set(AOO_INSTALL_PKG_CONFIG_DIR "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
        CACHE STRING "Path to AOO pkg-config file")
    mark_as_advanced(AOO_INSTALL_PKG_CONFIG_DIR)

    # CFLAGS
    if (AOO_NET)
        set(PKGCONFIG_CFLAGS "${PKGCONFIG_CFLAGS} -DAOO_NET=1")
    else()
        set(PKGCONFIG_CFLAGS "${PKGCONFIG_CFLAGS} -DAOO_NET=0")
    endif()

    set(PKGCONFIG_CFLAGS "${PKGCONFIG_CFLAGS} -DAOO_SAMPLE_SIZE=${AOO_SAMPLE_SIZE}")

    # NOTE: pkg-config files cannot really distinguish between shared and static
    # libraries. Fortunately, We can get away with not defining AOO_SHARED for
    # the shared library. On Linux and macOS this doesn't do anything; on Windows
    # we only get a minor performance penalty by omitting the __declspec(dllimport).
    # if (BUILD_SHARED_LIBS)
    #     set(PKGCONFIG_CFLAGS "${PKGCONFIG_CFLAGS} -DAOO_SHARED")
    # endif()

    # LDFLAGS
    if (LINUX)
        set(PKGCONFIG_LDFLAGS_PRIVATE "${PKGCONFIG_LDFLAGS_PRIVATE} -pthread")
    elseif (NOT MSVC)
        set(PKGCONFIG_LDFLAGS_PRIVATE "${PKGCONFIG_LDFLAGS_PRIVATE} -lpthread")
    endif()

    if (WIN32)
        set(PKGCONFIG_LDFLAGS_PRIVATE "${PKGCONFIG_LDFLAGS_PRIVATE} -lws2_32")
    endif()

    # Opus
    if (AOO_USE_OPUS)
        set(PKGCONFIG_REQUIRES "${PKGCONFIG_REQUIRES} opus")
        set(PKGCONFIG_CFLAGS "${PKGCONFIG_CFLAGS} -DAOO_USE_OPUS=1")
        if (MINGW)
            set(PKGCONFIG_LDFLAGS_PRIVATE "${PKGCONFIG_LDFLAGS_PRIVATE} -lssp")
        endif()
     else()
        set(PKGCONFIG_CFLAGS "${PKGCONFIG_CFLAGS} -DAOO_USE_OPUS=0")
    endif()

    # pkg-config module
    configure_file("../cmake/aoo.pc.in" "aoo.pc" @ONLY)

    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/aoo.pc"
        DESTINATION "${AOO_INSTALL_PKG_CONFIG_DIR}")
endif()
