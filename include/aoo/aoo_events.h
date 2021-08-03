#pragma once

#include "aoo_defines.h"

AOO_PACK_BEGIN

//-------------------------------------------------//

enum AooEventTypes
{
    kAooEventError = 0,
    // sink/source: received a ping from source/sink
    kAooEventPing,
    // source/sink: xruns occured
    kAooEventXRun,
    // source: invited by sink
    kAooEventInvite,
    // source: uninvited by sink
    kAooEventUninvite,
    // sink: source invitation timed out
    kAooEventInviteTimeout,
    // sink: source format changed
    kAooEventFormatChange,
    // sink: source added
    kAooEventSourceAdd,
    // sink: source removed
    kAooEventSourceRemove,
    // sink: stream started
    kAooEventStreamStart,
    // sink: stream stopped
    kAooEventStreamStop,
    // sink: stream changed state
    kAooEventStreamState,
    // sink: buffer underrun
    kAooEventBufferUnderrun,
    // sink: blocks have been lost
    kAooEventBlockLost,
    // sink: blocks arrived out of order
    kAooEventBlockReordered,
    // sink: blocks have been resent
    kAooEventBlockResent,
    // sink: block has been dropped by source
    kAooEventBlockDropped,
};

typedef AOO_STRUCT AooEventError
{
    AooEventType type;
    // platform specific error code for system errors
    AooInt32 errorCode;
    const AooChar *errorMessage;
} AooError_event;

typedef AOO_STRUCT AooEventXRun
{
    AooEventType type;
    AooInt32 count;
} AooEventXRun;

// source event
typedef AOO_STRUCT AooEventEndpoint
{
    AooEventType type;
    AooEndpoint endpoint;
} AooEventEndpoint;

#define AooEventSourceAdd AooEventEndpoint
#define AooEventSourceRemove AooEventEndpoint
#define AooEventInviteTimeout AooEventEndpoint
#define AooEventFormatTimeout AooEventEndpoint
#define AooEventBufferUnderrun AooEventEndpoint

// stream start/stop event

typedef AOO_STRUCT AooEventStreamStart
{
    AooEventType type;
    AooEndpoint endpoint;
    const AooCustomData * metadata;
} AooEventStreamStart;

#define AooEventStreamStop AooEventEndpoint

#define AooEventInvite AooEventStreamStart
#define AooEventUninvite AooEventEndpoint

// stream state event
typedef AooInt32 AooStreamState;

#define kAooStreamStateInactive 0
#define kAooStreamStateActive 1

typedef AOO_STRUCT AooEventStreamState
{
    AooEventType type;
    AooEndpoint endpoint;
    AooStreamState state;
} AooEventStreamState;

// block events
typedef AOO_STRUCT AooEventBlock
{
    AooEventType type;
    AooEndpoint endpoint;
    AooInt32 count;
} AooEventBlock;

#define AooEventBlockLost AooEventBlock
#define AooEventBlockReordered AooEventBlock
#define AooEventBlockResent AooEventBlock
#define AooEventBlockGap AooEventBlock
#define AooEventBlockDropped AooEventBlock

// ping event
typedef AOO_STRUCT AooEventPing {
    AooEventType type;
    AooEndpoint endpoint;
    AooNtpTime tt1;
    AooNtpTime tt2;
    AooNtpTime tt3;         // only for source
    AooInt32 lostBlocks;    // only for source
} AooEventPing;

// format events
typedef AOO_STRUCT AooEventFormatChange {
    AooEventType type;
    AooEndpoint endpoint;
    const AooFormat *format;
} AooEventFormatChange;

//--------------------- AOO_NET ---------------------//

#if USE_AOO_NET

enum AooNetEventTypes
{
    // generic events
    kAooNetEventError = 1000,
    kAooNetEventPing,
    // client events
    kAooNetEventDisconnect,
    kAooNetEventPeerHandshake,
    kAooNetEventPeerTimeout,
    kAooNetEventPeerJoin,
    kAooNetEventPeerLeave,
    kAooNetEventPeerMessage,
    // server events
    kAooNetEventUserAdd,
    kAooNetEventUserRemove,
    kAooNetEventUserJoin,
    kAooNetEventUserLeave,
    kAooNetEventGroupAdd,
    kAooNetEventGroupRemove,
    kAooNetEventUserGroupJoin,
    kAooNetEventUserGroupLeave
};

// generic events

typedef AOO_STRUCT AooNetEventError
{
    AooEventType type;
    // platform specific error code for system/socket errors
    AooInt32 errorCode;
    const AooChar *errorMessage;
} AooNetEventError;

typedef AOO_STRUCT AooNetEventPing
{
    AooEventType type;
    AooAddrSize addrlen;
    const void *address;
    AooNtpTime tt1;
    AooNtpTime tt2;
    AooNtpTime tt3; // only for clients
} AooNetEventPing;

// client events

#define AooNetEventDisconnect AooNetEventError

typedef AOO_STRUCT AooNetEventPeer
{
    AooEventType type;
    AooAddrSize addrlen;
    const void *address;
    const AooChar *groupName;
    const AooChar *userName;
    AooId userId;
} AooNetEventPeer;

#define AooNetEventPeerHandshake AooNetEventPeer
#define AooNetEventPeerTimeout AooNetEventPeer
#define AooNetEventPeerJoin AooNetEventPeer
#define AooNetEventPeerLeave AooNetEventPeer

typedef AOO_STRUCT AooNetEventPeerMessage
{
    AooEventType type;
    AooAddrSize addrlen;
    const void *address;
    const AooByte *data;
    AooInt32 size;
} AooNetEventPeerMessage;

// server events

typedef AOO_STRUCT AooNetEventUser
{
    AooEventType type;
    AooAddrSize addrlen;
    const void *address;
    const AooChar *userName;
    AooId userId;
} AooNetEventUser;

#define AooNetEventUserAdd AooNetEventUser
#define AooNetEventUserRemove AooNetEventUser
#define AooNetEventUserJoin AooNetEventUser
#define AooNetEventUserLeave AooNetEventUser

typedef AOO_STRUCT AooNetEventGroup
{
    AooEventType type;
    const AooChar *groupName;
} AooNetEventGroup;

#define AooNetEventGroupAdd AooNetEventGroup
#define AooNetEventGroupRemove AooNetEventGroup

typedef AOO_STRUCT AooNetEventUserGroup
{
    AooEventType type;
    AooId userId;
    const AooChar *userName;
    const AooChar *groupName;
} AooNetEventUserGroup;

#define AooNetEventUserGroupJoin AooNetEventUserGroup
#define AooNetEventUserGroupLeave AooNetEventUserGroup

#endif // USE_AOO_NET

//------------ user defined events -----------------//

// User defined event types (for custom AOO versions)
// must start from kAooEventUserDefined, for example:
//
// enum MyAooEventTypes
// {
//     kMyEvent1 = kAooEventUserDefined
//     kMyEvent2,
//     kMyEvent3,
//     ...
// };
#define kAooEventUserDefined 10000

//-------------------------------------------------//

AOO_PACK_END
