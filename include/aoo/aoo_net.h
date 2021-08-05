/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

// AOO_NET is an embeddable UDP punch hole library for creating dynamic
// peer-2-peer networks over the public internet. It has been designed
// to seemlessly interoperate with the AOO streaming library.
//
// The implementation is largely based on the techniques described in the paper
// "Peer-to-Peer Communication Across Network Address Translators"
// by Ford, Srisuresh and Kegel (https://bford.info/pub/net/p2pnat/)
//
// It uses TCP over SLIP to reliable exchange meta information between peers.
//
// The UDP punch hole server runs on a public endpoint and manages the public
// and local IP endpoint addresses of all the clients.
// It can host multiple peer-2-peer networks which are organized as called groups.
//
// Each client connects to the server, logs in as a user, joins one ore more groups
// and in turn receives the public and local IP endpoint addresses from its peers.
//
// Currently, users and groups are automatically created on demand, but later
// we might add the possibility to create persistent users and groups on the server.
//
// Later we might add TCP connections between peers, so we can reliably exchange
// additional data, like chat messages or arbitrary OSC messages.
//
// Also we could support sending additional notifications from the server to all clients.

#pragma once

#include "aoo_defines.h"

AOO_PACK_BEGIN

//------------- default values --------------------//

#ifndef AOO_NET_RELAY_ENABLE
 #define AOO_NET_RELAY_ENABLE 1
#endif

#ifndef AOO_NET_NOTIFY_ON_SHUTDOWN
 #define AOO_NET_NOTIFY_ON_SHUTDOWN 0
#endif

//------------- public OSC interface --------------//

#define kAooNetMsgServer "/server"
#define kAooNetMsgServerLen 7

#define kAooNetMsgClient "/client"
#define kAooNetMsgClientLen 7

#define kAooNetMsgPeer "/peer"
#define kAooNetMsgPeerLen 5

#define kAooNetMsgRelay "/relay"
#define kAooNetMsgRelayLen 6

#define kAooNetMsgPing "/ping"
#define kAooNetMsgPingLen 5

#define kAooNetMsgReply "/reply"
#define kAooNetMsgReplyLen 6

#define kAooNetMsgMessage "/msg"
#define kAooNetMsgMessageLen 4

#define kAooNetMsgLogin "/login"
#define kAooNetMsgLoginLen 6

#define kAooNetMsgRequest "/request"
#define kAooNetMsgRequestLen 8

#define kAooNetMsgGroup "/group"
#define kAooNetMsgGroupLen 6

#define kAooNetMsgJoin "/join"
#define kAooNetMsgJoinLen 5

#define kAooNetMsgLeave "/leave"
#define kAooNetMsgLeaveLen 6

//------------ requests/replies ---------------//

typedef void (AOO_CALL *AooNetCallback)
        (void *user, AooError result, const void *reply);

typedef AOO_STRUCT AooNetReplyError
{
    // discriptive error message
    const AooChar *errorMessage;
    // platform-specific error code for socket/system errors
    AooInt32 errorCode;
} AooNetReplyError;

typedef AooInt32 AooNetRequestType;

enum AooNetRequestTypes
{
    kAooNetRequestConnect = 0,
    kAooNetRequestDisconnect,
    kAooNetRequestJoinGroup,
    kAooNetRequestLeaveGroup
};

// connect

typedef AOO_STRUCT AooNetRequestConnect
{
    const AooChar *hostName;
    AooInt32 port;
    const AooChar *userName;
    const AooChar *userPwd;
    AooFlag flags;
} AooNetRequestConnect;

enum AooNetServerFlags
{
    kAooNetServerRelay = 0x01
};

typedef AOO_STRUCT AooNetReplyConnect
{
    AooId userId;
    AooFlag serverFlags;
} AooNetReplyConnect;

// join/leave group

typedef AOO_STRUCT AooNetRequestGroup
{
    const AooChar *groupName;
    const AooChar *groupPwd;
    AooFlag flags;
} AooNetRequestGroup;

#define AooNetRequestJoinGroup AooNetRequestGroup
#define AooNetRequestLeaveGroup AooNetRequestGroup

//------------------- misc ------------------------//

// used in AooClient_sendMessage / AooClient::sendMessage
enum AooNetMessageFlags
{
    kAooNetMessageReliable = 0x01
};

//-----------------------------------------------//

AOO_PACK_END
