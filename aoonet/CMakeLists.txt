cmake_minimum_required (VERSION 2.8)

set(INCLUDE "../include/aoonet")

set(COMMON "../common")

file(GLOB AOONET_HEADERS "${INCLUDE}/*.h" "${INCLUDE}/*.hpp" "src/*.hpp"
    "${COMMON}/lockfree.hpp" "${COMMON}/net_utils.hpp"
    "${COMMON}/sync.hpp" "${COMMON}/time.hpp" "${COMMON}/utils.hpp")

file(GLOB AOONET_SRC ${AOONET_HEADERS} ${OSCPACK_SRC} "src/*.cpp"
    "${COMMON}/net_utils.cpp" "${COMMON}/sync.cpp" "${COMMON}/time.cpp")

# static library
add_library(${AOONET_STATIC} STATIC ${AOONET_SRC})

target_compile_definitions(${AOONET_STATIC} PUBLIC "-DAOO_STATIC")

# shared library
option(BUILD_AOONET_SHARED "build 'aoonet' as shared library" ON)

if (BUILD_AOONET_SHARED)
    add_library(${AOONET_SHARED} SHARED ${AOONET_SRC})

    target_compile_definitions(${AOONET_SHARED} PUBLIC "-DAOO_BUILD")
    target_link_libraries(${AOONET_SHARED} ${MD5_LIB} ${OSCPACK_LIB} ${LIBS})
    if (WIN32 OR MINGW)
        target_compile_definitions(${AOONET_SHARED} PUBLIC "-DDLL_EXPORT")
        target_link_libraries(${AOONET_SHARED} "ws2_32")
    endif()

    # install shared libraries
    install(TARGETS ${AOONET_SHARED}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    # install public headers (note the trailing slash!)
    install(DIRECTORY "${INCLUDE}/"
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${AOONET_SHARED})
endif()
