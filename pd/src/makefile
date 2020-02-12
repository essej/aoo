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
class.sources = aoo_pack~.cpp aoo_unpack~.cpp aoo_route.cpp aoo_send~.cpp aoo_receive~.cpp

define forWindows
    aoo_send~.class.ldlibs = -lws2_32
    aoo_receive~.class.ldlibs = -lws2_32
endef

AOO = ../../lib/src

cflags = -I$(AOO) \
    -Wno-unused -Wno-unused-parameter \
    -DLOGLEVEL=2

common.sources = \
    $(AOO)/aoo.cpp \
    $(AOO)/lib/oscpack/osc/OscTypes.cpp \
    $(AOO)/lib/oscpack/osc/OscReceivedElements.cpp \
    $(AOO)/lib/oscpack/osc/OscOutboundPacketStream.cpp

# all extra files to be included in binary distribution of the library
datafiles =

# include Makefile.pdlibbuilder from submodule directory 'pd-lib-builder'

PDLIBBUILDER_DIR=pd-lib-builder
include $(PDLIBBUILDER_DIR)/Makefile.pdlibbuilder
