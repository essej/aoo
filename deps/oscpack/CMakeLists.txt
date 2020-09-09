# simplified version which only builds the "osc" folder

add_library(${OSCPACK_LIB} STATIC
    "osc/OscTypes.h"
    "osc/OscTypes.cpp"
    "osc/OscHostEndianness.h"
    "osc/OscException.h"
    "osc/OscPacketListener.h"
    "osc/MessageMappingOscPacketListener.h"
    "osc/OscReceivedElements.h"
    "osc/OscReceivedElements.cpp"
    "osc/OscPrintReceivedElements.h"
    "osc/OscPrintReceivedElements.cpp"
    "osc/OscOutboundPacketStream.h"
    "osc/OscOutboundPacketStream.cpp")
