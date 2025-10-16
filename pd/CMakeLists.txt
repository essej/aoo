# Pd directories:
if (WIN32)
    # This is how PD_INCLUDE_DIR and PD_BIN_DIR are set (in descending order of priority):
    # 1. set from the command line or in the GUI (ignores PD_PATH)
    # 2. deduced from PD_PATH, if set
    # 3. otherwise try to find Pd installation and set PD_PATH, PD_INCLUDE_DIR and PD_BIN_DIR
    set(PD_INCLUDE_DIR "" CACHE PATH "Path to Pd headers ('src' folder)")
    set(PD_BIN_DIR "" CACHE PATH "Path to Pd executables ('bin' folder)")
    set(PD_PATH "" CACHE PATH "Path to Pd application")
    if (NOT (PD_INCLUDE_DIR AND PD_BIN_DIR))
        if (NOT PD_PATH)
            # try to find Pd installation
            set(_pddir "${PROGRAMFILES}/Pd")
            if (EXISTS "${_pddir}")
                set(PD_PATH "${_pddir}" CACHE PATH "Path to Pd application" FORCE)
            else()
                message(WARNING "No Pd installation found! "
                    "Please set PD_PATH or PD_INCLUDE_DIR + PD_BIN_DIR")
            endif()
        endif()
        if (PD_PATH)
            # set PD_INCLUDE_DIR and PD_BIN_DIR
            set(PD_INCLUDE_DIR "${PD_PATH}/src" CACHE PATH "Path to Pd headers ('src' folder)" FORCE)
            set(PD_BIN_DIR "${PD_PATH}/bin" CACHE PATH "Path to Pd executables ('bin' folder)" FORCE)
        endif()
    endif()
elseif(APPLE)
    # This is how PD_INCLUDE_DIR is set (in descending order of priority):
    # 1. set from the command line (ignores PD_PATH)
    # 2. deduced from PD_PATH, if set
    # 3. otherwise try to find Pd installation and set PD_PATH and PD_INCLUDE_DIR
    set(PD_INCLUDE_DIR "" CACHE PATH "Path to Pd headers ('src' folder)")
    set(PD_PATH "" CACHE PATH "Path to Pd application")
    if (NOT PD_INCLUDE_DIR)
        if (NOT PD_PATH)
            # try to find Pd installation
            file(GLOB _globresult "/Applications/Pd*.app")
            if (_globresult)
                list(GET _globresult 0 _pddir)
                set(PD_PATH "${_pddir}" CACHE PATH "Path to Pd application" FORCE)
            else()
                message(WARNING "No Pd installation found! Please set PD_PATH or PD_INCLUDE_DIR")
            endif()
        endif()
        if (PD_PATH)
            # set PD_INCLUDE_DIR
            set(PD_INCLUDE_DIR "${PD_PATH}/Contents/Resources/src" CACHE PATH
                "Path to Pd headers ('src' folder)" FORCE)
        endif()
    endif()
else()
    # Linux
    set(PD_INCLUDE_DIR "" CACHE PATH "Path to Pd headers")
    if (NOT PD_INCLUDE_DIR)
        # try to find Pd installation
        unset(PD_INCLUDE_DIR CACHE)
        find_path(PD_INCLUDE_DIR NAMES "m_pd.h"
            PATHS "/usr/include/pd" "/usr/local/include/pd"
            DOC "Path to Pd headers"
        )
        if (NOT PD_INCLUDE_DIR)
            message(WARNING "Could not find Pd headers. "
                "Install puredata-dev or manually set PD_INCLUDE_DIR.")
        endif()
    endif()
endif()

if (PD_PATH)
    message(STATUS "Pd application: ${PD_PATH}")
endif()
message(STATUS "Pd include directory: ${PD_INCLUDE_DIR}")
if (WIN32)
    message(STATUS "Pd binary directory: ${PD_BIN_DIR}")
endif()

# install directory:
if (WIN32)
    set(PD_INSTALL_DIR "${APPDATA}/Pd" CACHE PATH "Pd install directoy")
elseif(APPLE)
    set(PD_INSTALL_DIR "~/Library/Pd" CACHE PATH "Pd install directory")
else()
    set(PD_INSTALL_DIR "/usr/local/lib/pd-externals" CACHE PATH "Pd install directory")
endif()
message(STATUS "Pd install directory: ${PD_INSTALL_DIR}")

# the Pd external
add_library(pd MODULE
    "src/aoo_client.cpp"
    "src/aoo_common.hpp"
    "src/aoo_common.cpp"
    "src/aoo_node.cpp"
    "src/aoo_receive~.cpp"
    "src/aoo_send~.cpp"
    "src/aoo_server.cpp"
    "src/aoo_setup.cpp")

if (BUILD_SHARED_LIBS)
    # we can't take these from the static aoo library,
    # so we must add them to our target.
    target_sources(pd PRIVATE
        "../common/net_utils.cpp"
        "../common/net_utils.hpp"
        "../common/log.hpp"
        "../common/priority_queue.hpp"
        "../common/sync.cpp"
        "../common/sync.hpp"
        "../common/time.cpp"
        "../common/time.hpp"
    )
    if (WIN32)
        target_link_libraries(pd PRIVATE ws2_32)
    endif()
endif()

target_include_directories(pd PRIVATE ${PD_INCLUDE_DIR})

target_link_libraries(pd PRIVATE aoo aoo_common aoo_binary)

# multi-instance support
option(PD_MULTI_INSTANCE "Pd multi-instance support (for libpd)" OFF)
message(STATUS "Pd multi-instance support: ${PD_MULTI_INSTANCE}")
if (PD_MULTI_INSTANCE)
    target_compile_definitions(pd PRIVATE PDINSTANCE)
endif()

# specify float size
set(PD_FLOAT_SIZE 32 CACHE STRING "Pd float size")
set_property(CACHE PD_FLOAT_SIZE PROPERTY STRINGS "32;64")
message(STATUS "Pd float size: ${PD_FLOAT_SIZE}")
target_compile_definitions(pd PRIVATE PD_FLOATSIZE=${PD_FLOAT_SIZE})

# plugin extension
set(PD_EXTENSION "" CACHE STRING "Pd plugin extension")
if (NOT PD_EXTENSION)
    # default extensions
    if (WIN32)
        set(SUFFIX ".dll")
    elseif(APPLE)
        set(SUFFIX ".pd_darwin")
    elseif(LINUX)
        set(SUFFIX ".pd_linux")
    else()
        set(SUFFIX "${CMAKE_SHARED_MODULE_SUFFIX}")
    endif()
else()
    set(SUFFIX ".${PD_EXTENSION}")
endif()
message(STATUS "Pd extension: ${SUFFIX}")

# output name
set_target_properties(pd PROPERTIES
    PREFIX "" # no prefix!
    OUTPUT_NAME "aoo"
    SUFFIX ${SUFFIX})

# platform specific definitions and options
if (WIN32)
    if (MINGW)
        target_link_options(pd PRIVATE --enable-auto-import)
        target_link_libraries(pd PRIVATE "${PD_BIN_DIR}/pd.dll")
    else()
        target_link_libraries(pd PRIVATE "${PD_BIN_DIR}/pd.lib")
    endif()
elseif(APPLE)
    target_link_options(pd PRIVATE -undefined suppress -flat_namespace -bundle)
else()
    target_link_options(pd PRIVATE -rdynamic -fPIC)
endif()

# installation
# TODO: install PDB file (Visual Studio)
install(TARGETS pd DESTINATION "${PD_INSTALL_DIR}/aoo")

install(FILES
    "aoo_send~-help.pd"
    "aoo_receive~-help.pd"
    "aoo_client-help.pd"
    "aoo_server-help.pd"
    "mc_aoo_send~.pd"
    "mc_aoo_receive~.pd"
    "../README.md"
    "../LICENSE"
    DESTINATION "${PD_INSTALL_DIR}/aoo")
