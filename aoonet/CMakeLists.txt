cmake_minimum_required (VERSION 2.8)

set(INCLUDE "../include/aoonet")

set(AOONET_PUBLIC_HEADERS "${INCLUDE}/aoonet.h" "${INCLUDE}/aoonet.hpp")

file(GLOB AOONET_HEADERS "src/*.hpp")

file(GLOB AOONET_SRC ${AOONET_PUBLIC_HEADERS} ${AOONET_HEADERS}
    ${AOO_COMMON_SRC} ${OSCPACK_SRC} "src/*.cpp")

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

    set_target_properties(${AOONET_SHARED} PROPERTIES
        PUBLIC_HEADER "${AOONET_PUBLIC_HEADERS}")

    install(TARGETS ${AOONET_SHARED}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${AOONET_SHARED})
endif()
