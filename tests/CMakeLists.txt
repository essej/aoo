set(TEST_C_INTERFACE "c_interface")

add_executable(${TEST_C_INTERFACE} "${TEST_C_INTERFACE}.c")

set(TEST_C_STANDARD "c99")

set(STATIC_LIBAOO TRUE)

if (STATIC_LIBAOO)
    target_compile_definitions(${TEST_C_INTERFACE} PUBLIC "-DAOO_STATIC")
    list(APPEND TEST_LIBS ${AOO_STATIC} ${OPUS_LIB})
else()
    list(APPEND TEST_LIBS ${AOO_SHARED})
endif()

if (NOT MSVC)
    target_compile_options(${TEST_C_INTERFACE} PUBLIC "-std=${TEST_C_STANDARD}" "-pedantic")
endif()

target_link_libraries(${TEST_C_INTERFACE} ${TEST_LIBS} ${LIBS})
if (WIN32)
    target_link_libraries(${TEST_C_INTERFACE} "ws2_32")
endif()
