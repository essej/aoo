cmake_minimum_required (VERSION 2.8)

set(TEST_C_INTERFACE "c_interface")

# platform specific linker flags
if (NOT MSVC)
    list(APPEND TEST_C_LIBS "-lpthread")
endif()

if ((LINUX OR MINGW) AND STATIC_LIBS)
    list(APPEND TEST_C_LIBS "-static-libgcc")
    list(APPEND TEST_CXX_LIBS "-static-libstdc++")
endif()

add_executable(${TEST_C_INTERFACE} "${TEST_C_INTERFACE}.c")

set(TEST_C_STANDARD "c99")

if (NOT MSVC)
    target_compile_options(${TEST_C_INTERFACE} PUBLIC "-std=${TEST_C_STANDARD}" "-pedantic")
endif()

target_link_libraries(${TEST_C_INTERFACE} ${AOO_SHARED} ${TEST_C_LIBS})
