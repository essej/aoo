#--------------------------------------------------------------
# FindPortAudio
#--------------------------------------------------------------
#
# Finds the Portaudio library
#
# Imported Targets:
#
# PortAudio::portaudio - the PortAudio library
#
#---------------------------------------------------------------

# Prefer cmake config over pkg-config because it is more portable.

find_package(PkgConfig QUIET)

# first try cmake config
find_package(PortAudio CONFIG QUIET)

if (PortAudio_FOUND)
    if (NOT TARGET PortAudio::portaudio)
        # portaudio v19.7 >=
        if (TARGET portaudio)
            add_library(PortAudio::portaudio ALIAS portaudio)
        else()
            message(STATUS "missing target 'portaudio' in PortAudio package")
            set(PortAudio_FOUND FALSE)
        endif()
    endif()
endif()

if (NOT PortAudio_FOUND)
    # then try pkg-config
    if (PkgConfig_FOUND)
        pkg_check_modules(PortAudio IMPORTED_TARGET portaudio-2.0)
        if (PortAudio_FOUND)
            add_library(PortAudio::portaudio INTERFACE IMPORTED)
            target_link_libraries(PortAudio::portaudio INTERFACE PkgConfig::PortAudio)
        endif()
    endif()
endif()

if (NOT PortAudio_FOUND)
    # TODO: fall back to find_path() and find_library()
    set(PortAudio_NOT_FOUND_MESSAGE "Could not find PortAudio! Set PortAudio_DIR or make sure that portaudio-2.0.pc can be found.")
    if (PortAudio_FIND_REQUIRED)
        message(FATAL_ERROR ${PortAudio_NOT_FOUND_MESSAGE})
    else()
        message(STATUS ${PortAudio_NOT_FOUND_MESSAGE})
    endif()
endif()
