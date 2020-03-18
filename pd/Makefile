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

# input source file
# weird HACK for pd-lib-builder: use .cpp extension although the actual sources are .c files;
# this causes the sources files to be compiled with gcc but linked with g++ (needed for aoo lib).

class.sources = src/aoo_pack~.cpp src/aoo_unpack~.cpp src/aoo_route.cpp \
                src/aoo_send~.cpp src/aoo_receive~.cpp

ldlibs = -lopus

define forWindows
    ldlibs += -lws2_32
endef

define forDarwin
    ldlibs += -lpthread
endef

define forLinux
    ldlibs += -pthread
endef

AOO = ../lib

cflags = -I$(AOO) \
    -I$(AOO)/src \
    -I$(AOO)/src/lib \
    -Wno-unused -Wno-unused-parameter \
    -DLOGLEVEL=2 \
    -DAOO_PD_OSCTIMEHACK=0 \
    -DAOO_TIMEFILTER_BANDWIDTH=0.0001 \
    -DAOO_DEBUG_DLL=0 \
    -DAOO_DEBUG_OSCTIME=0 \
    -DAOO_DEBUG_RESAMPLING=0

ldlibs = -lopus

common.sources = \
    src/aoo_common.cpp \
    src/aoo_net.cpp \
    $(AOO)/src/aoo_imp.cpp \
    $(AOO)/src/aoo_source.cpp \
    $(AOO)/src/aoo_sink.cpp \
    $(AOO)/src/aoo_pcm.cpp \
    $(AOO)/src/aoo_opus.cpp \
    $(AOO)/src/lib/oscpack/osc/OscTypes.cpp \
    $(AOO)/src/lib/oscpack/osc/OscReceivedElements.cpp \
    $(AOO)/src/lib/oscpack/osc/OscOutboundPacketStream.cpp


# all extra files to be included in binary distribution of the library
datafiles = aoo_pack~-test.pd aoo_receive~-test.pd aoo_send~-test.pd aoo-test.pd aoo_unpack~-test.pd

# include Makefile.pdlibbuilder from submodule directory 'pd-lib-builder'
PDLIBBUILDER_DIR=pd-lib-builder
include $(PDLIBBUILDER_DIR)/Makefile.pdlibbuilder
