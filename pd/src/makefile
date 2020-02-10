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

# input source file (class name == source file basename)
class.sources = aoo_pack~.cpp aoo_unpack~.cpp aoo_route.cpp

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
