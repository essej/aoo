# Makefile to build library 'aoo' for Pure Data.
# Needs Makefile.pdlibbuilder as helper makefile for platform-dependent build
# settings and rules.
#
# use : make PDDIR=/path/to/pure-data/
#
# The following command will build the external and install the distributable
# files into a subdirectory called build :
#
# make install PDDIR=/path/to/pure-data/ PDLIBDIR=./build

# library name
lib.name = aoo

# input source files
class.sources = \
    src/aoo_pack~.c \
    src/aoo_unpack~.c \
    src/aoo_route.c \
    src/aoo_send~.c \
    src/aoo_receive~.c \
    src/aoo_server.c \
    src/aoo_client.c \
    $(empty)

AOO = ../lib
DEPS = ../deps

## oscpack
## set 'system_oscpack' to 'yes' (and set the appropriate
## OSCPACK_CFLAGS/OSCPACK_LIBS) to use a system-provided
## oscpack implementation
OSCPACK = $(DEPS)/oscpack

ifneq ($(system_oscpack),yes)
OSCPACK_CFLAGS = -I$(OSCPACK)/..
OSCPACK_LIBS =
endif

## md5
## set 'system_md5' to 'yes' (and set the appropriate
## MD5_CFLAGS/MD5_LIBS) to use a system-provided
## md5 implementation
MD5 = $(DEPS)/md5

ifneq ($(system_md5),yes)
MD5_CFLAGS = -I$(MD5)/..
MD5_LIBS =
endif


OPUS_CFLAGS =
OPUS_LIBS = -lopus

cflags = \
    -I$(AOO) \
    -I$(AOO)/src \
    -I$(AOO)/src/lib \
    -Wno-unused -Wno-unused-parameter \
    -fvisibility=hidden \
    -DNDEBUG \
    -DAOO_STATIC \
    -DLOGLEVEL=1 \
    -DAOO_TIMEFILTER_BANDWIDTH=0.0001 \
    -DAOO_DEBUG_DLL=0 \
    -DAOO_DEBUG_RESAMPLING=0 \
    -DAOO_DEBUG_BLOCK_BUFFER=0 \
    $(OPUS_CFLAGS) \
    $(OSCPACK_CFLAGS) \
    $(MD5_CFLAGS) \
    $(empty)

ldlibs =

make-lib-executable=yes

lib.setup.sources = src/aoo_setup.c

shared.sources = \
    src/aoo_node.c \
    src/aoo_common.c \
    src/aoo_net.c \
    $(AOO)/src/common.cpp \
    $(AOO)/src/sync.cpp \
    $(AOO)/src/time.cpp \
    $(AOO)/src/source.cpp \
    $(AOO)/src/sink.cpp \
    $(AOO)/src/server.cpp \
    $(AOO)/src/client.cpp \
    $(AOO)/src/net_utils.cpp \
    $(AOO)/src/codec_pcm.cpp \
    $(AOO)/src/codec_opus.cpp \
    $(empty)

ifneq ($(system_oscpack),yes)
shared.sources += \
    $(OSCPACK)/osc/OscTypes.cpp \
    $(OSCPACK)/osc/OscReceivedElements.cpp \
    $(OSCPACK)/osc/OscOutboundPacketStream.cpp \
    $(empty)
endif
ifneq ($(system_md5),yes)
shared.sources += \
    $(MD5)/md5.c \
    $(empty)
endif

# hack to set the C++ standard only for the shared library
$(shared.sources:.cpp=.o): cxx.flags += -std=c++14

ldlibs += $(OSCPACK_LIBS) $(MD5_LIBS)

define forWindows
    ldlibs += -lws2_32 $(OPUS_LIBS) -static-libstdc++ -static-libgcc
endef

define forDarwin
    cflags += -mmacosx-version-min=10.9 -DAOO_TIMEFILTER_CHECK=0
    ldlibs += -lpthread $(OPUS_LIBS)
endef

define forLinux
    ldlibs += -pthread $(OPUS_LIBS)
endef

# all extra files to be included in binary distribution of the library
datafiles = aoo_receive~-help.pd aoo_send~-help.pd
#aoo_pack~-help.pd  aoo-help.pd aoo_unpack~-help.pd aoo_route-help.pd

# include Makefile.pdlibbuilder from submodule directory 'pd-lib-builder'
PDLIBBUILDER_DIR=pd-lib-builder
include $(PDLIBBUILDER_DIR)/Makefile.pdlibbuilder
