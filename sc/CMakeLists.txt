# SuperCollider API:

# This is how SC_INCLUDE_DIR is set (in descending order of priority):
# 1. set from the command line or in the GUI (ignores SC_PATH)
# 2. deduced from SC_PATH, if set
# 3. Linux: try to find SuperCollider installation and set SC_INCLUDE_DIR
set(SC_INCLUDE_DIR "" CACHE PATH "Path to SuperCollider headers")
set(SC_PATH "" CACHE PATH "(Optional) Path to SuperCollider repository")
if (NOT SC_INCLUDE_DIR)
    if (SC_PATH)
        # set SC_INCLUDE_DIR
        set(SC_INCLUDE_DIR "${SC_PATH}/include" CACHE PATH "Path to SuperCollider headers" FORCE)
    else()
        if (WIN32 OR APPLE)
            message(WARNING "Please set SC_PATH or SC_INCLUDE_DIR")
        else()
            # Linux: try to find SC installation
            unset(SC_INCLUDE_DIR CACHE)
            find_path(SC_INCLUDE_DIR NAMES "plugin_interface/SC_PlugIn.h"
                PATHS "/usr/include/SuperCollider" "/usr/local/include/SuperCollider"
                DOC "Path to SuperCollider headers"
            )
            if (NOT SC_INCLUDE_DIR)
                message(WARNING "Could not find SuperCollider headers. Please set SC_PATH or SC_INCLUDE_DIR.")
            endif()
        endif()
    endif()
endif()

message(STATUS "SC repository: ${SC_PATH}")
message(STATUS "SC include directory: ${SC_INCLUDE_DIR}")

# install directory
if (WIN32)
    set(SC_INSTALL_DIR "${LOCALAPPDATA}/SuperCollider/Extensions/" CACHE PATH "SC install directory")
elseif(APPLE)
    set(SC_INSTALL_DIR "~/Library/Application Support/SuperCollider/Extensions/" CACHE PATH "SC install directoy")
else()
    # Linux
    set(SC_INSTALL_DIR "~/.local/share/SuperCollider/Extensions/" CACHE PATH "SC install directoy")
endif()
message(STATUS "SC install directory: ${SC_INSTALL_DIR}")

option(SC_SUPERNOVA "Build Supernova plugins" ON)
message(STATUS "Build Supernova plugins: ${SC_SUPERNOVA}")

# common properties
add_library(sc_common INTERFACE)

target_sources(sc_common INTERFACE
    "src/Aoo.cpp" "src/Aoo.hpp"
    "src/AooClient.cpp" "src/AooClient.hpp"
    "src/AooNode.cpp" "src/AooNode.hpp"
    "src/AooReceive.cpp" "src/AooReceive.hpp"
    "src/AooSend.cpp" "src/AooSend.hpp"
    "src/AooServer.cpp" "src/AooServer.hpp"
    "src/rt_shared_ptr.hpp"
)

if (BUILD_SHARED_LIBS)
    # we can't take these from the static aoo library,
    # so we must add them to our target.
    target_sources(sc_common INTERFACE
        "../common/net_utils.cpp"
        "../common/net_utils.hpp"
        "../common/log.hpp"
        "../common/sync.cpp"
        "../common/sync.hpp"
        "../common/time.cpp"
        "../common/time.hpp"
        # oscpack
        $<TARGET_OBJECTS:aoo_oscpack>
    )
    if (WIN32)
        target_link_libraries(sc_common INTERFACE ws2_32)
    endif()
endif()

target_include_directories(sc_common INTERFACE
    "${SC_INCLUDE_DIR}/plugin_interface"
    "${SC_INCLUDE_DIR}/common"
)

# plugin extension
if (WIN32 OR APPLE)
    set(SUFFIX ".scx")
else()
    set(SUFFIX "${CMAKE_SHARED_MODULE_SUFFIX}")
endif()

# the SC extension
add_library(sc MODULE)

target_link_libraries(sc PRIVATE
    sc_common aoo aoo_common aoo_binary)

# output name
set_target_properties(sc PROPERTIES
    PREFIX "" # no prefix!
    OUTPUT_NAME "aoo"
    SUFFIX ${SUFFIX}
)

# the Supernova extension
if (SC_SUPERNOVA)
    add_library(supernova MODULE)

    target_link_libraries(supernova PRIVATE
        sc_common aoo aoo_common aoo_binary)

    # output name
    set_target_properties(supernova PROPERTIES
        PREFIX "" # no prefix!
        OUTPUT_NAME "aoo_supernova"
        SUFFIX ${SUFFIX}
    )
endif()

# installation
install(TARGETS sc DESTINATION "${SC_INSTALL_DIR}/aoo/plugins")
if (SC_SUPERNOVA)
    install(TARGETS supernova DESTINATION "${SC_INSTALL_DIR}/aoo/plugins")
endif()

install(DIRECTORY "classes" DESTINATION "${SC_INSTALL_DIR}/aoo")

install(DIRECTORY "HelpSource" DESTINATION "${SC_INSTALL_DIR}/aoo")

install(FILES "../README.md" "../LICENSE" DESTINATION "${SC_INSTALL_DIR}/aoo")
