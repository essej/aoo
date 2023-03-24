/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include "aoo_config.h"
#include "aoo_defines.h"
#include "aoo_types.h"

AOO_PACK_BEGIN

/*--------------------------------------------*/

/** \brief request types */
AOO_ENUM(AooRequestType)
{
    /** error response */
    kAooRequestError = 0,
    /** connect to server */
    kAooRequestConnect,
    /** query public IP + server IP */
    kAooRequestQuery,
    /** login to server */
    kAooRequestLogin,
    /** disconnect from server */
    kAooRequestDisconnect,
    /** join group */
    kAooRequestGroupJoin,
    /** leave group */
    kAooRequestGroupLeave,
    /** update group */
    kAooRequestGroupUpdate,
    /** update user */
    kAooRequestUserUpdate,
    /** custom request */
    kAooRequestCustom
};

/*----------------- request ------------------*/

/** \brief common header for all request structures */
#define AOO_REQUEST_HEADER \
    AooRequestType type; \
    AooUInt32 structSize;

/** \brief basic request */
typedef struct AooRequestBase
{
    AOO_REQUEST_HEADER
} AooRequestBase;

/** \brief helper function for initializing a request structure */
#define AOO_REQUEST_INIT(ptr, name, field) \
    (ptr)->type = kAooRequest##name; \
    (ptr)->structSize = AOO_STRUCT_SIZE(AooRequest##name, field);

/*----------------- response ------------------*/

/** \brief common header for all response structures */
#define AOO_RESPONSE_HEADER AOO_REQUEST_HEADER

/** \brief basic response */
typedef struct AooResponseBase
{
    AOO_RESPONSE_HEADER
} AooResponseBase;

/** \brief helper function for initializing a reponse structure */
#define AOO_RESPONSE_INIT(ptr, name, field) \
    (ptr)->type = kAooRequest##name; \
    (ptr)->structSize = AOO_STRUCT_SIZE(AooResponse##name, field);


/*----------------- error ------------------*/

/** \brief error response */
typedef struct AooResponseError
{
    AOO_RESPONSE_HEADER
    /** platform- or user-specific error code */
    AooInt32 errorCode;
    /** discriptive error message */
    const AooChar *errorMessage;
} AooResponseError;

/*--------- connect (client-side) ---------*/

/** \brief connection request */
typedef struct AooRequestConnect
{
    AOO_REQUEST_HEADER
    AooIpEndpoint address;
    const AooChar *password;
    const AooData *metadata;
} AooRequestConnect;

/** \brief connection response */
typedef struct AooResponseConnect
{
    AOO_RESPONSE_HEADER
    AooId clientId; /* client-only */
    const AooData *metadata;
} AooResponseConnect;

/*--------- disconnect (client-side) ----------*/

typedef AooRequestBase AooRequestDisconnect;

typedef AooResponseBase AooResponseDisconnect;

/*----------- query (server-side) -------------*/

/** \brief query request */
typedef struct AooRequestQuery
{
    AOO_REQUEST_HEADER
    AooSockAddr replyAddr;
    AooSendFunc replyFunc;
    void *replyContext;
} AooRequestQuery;

/** \brief query response */
typedef struct AooResponseQuery
{
    AOO_RESPONSE_HEADER
    AooIpEndpoint serverAddress;
} AooResponseQuery;

/*----------- login (server-side) ------------*/

/** \brief login request */
typedef struct AooRequestLogin
{
    AOO_REQUEST_HEADER
    const AooChar *password;
    const AooData *metadata;
} AooRequestLogin;

/** \brief login response */
typedef struct AooResponseLogin
{
    AOO_RESPONSE_HEADER
    const AooData *metadata;
} AooResponseLogin;

/*-------- join group (server/client) -------*/

/** \brief request for joining a group */
typedef struct AooRequestGroupJoin
{
    AOO_REQUEST_HEADER
    /* group */
    const AooChar *groupName;
    const AooChar *groupPwd;
    AooId groupId; /* kAooIdInvalid if group does not exist (yet) */
    AooFlag groupFlags;
    /** The client who creates the group may provide group metadata
     * in AooClient::joinGroup(). By default, the server just stores
     * the metadata and sends it to all subsequent users via this field.
     * However, it may also intercept the request and validate/modify the
     * metadata, or provide any kind of metadata it wants, by setting
     * AooResponseGroupJoin::groupMetadata. */
    const AooData *groupMetadata;
    /* user */
    const AooChar *userName;
    const AooChar *userPwd;
    AooId userId; /* kAooIdInvalid if user does not exist (yet) */
    AooFlag userFlags;
    /** Each client may provide user metadata in AooClient::joinGroup().
     * By default, the server just stores the metadata and sends it to all
     * peers via AooEventPeer::metadata. However, it may also intercept
     * the request and validate/modify the metadata, or provide any kind of
     * metadata it wants, by setting AooResponseGroupJoin::userMetadata. */
    const AooData *userMetadata;
    /* other */
    /** (optional) Relay address provided by the user/client. The server will
     * forward it to all peers. */
    const AooIpEndpoint *relayAddress;
} AooRequestGroupJoin;

/** \brief response for joining a group */
typedef struct AooResponseGroupJoin
{
    AOO_RESPONSE_HEADER
    /* group */
    /** group ID generated by the server */
    AooId groupId;
    AooFlag groupFlags;
    /** (optional) group metadata validated/modified by the server. */
    const AooData *groupMetadata;
    /* user */
    /** user Id generated by the server */
    AooId userId;
    AooFlag userFlags;
    /** (optional) user metadata validated/modified by the server. */
    const AooData *userMetadata;
    /* other */
    /** (optional) private metadata that is only sent to the client.
     * For example, this can be used for state synchronization. */
    const AooData *privateMetadata;
    /** (optional) relay address provided by the server.
      * For example, the AOO server might provide a group with a
      * dedicated UDP relay server. */
    const AooIpEndpoint *relayAddress;
} AooResponseGroupJoin;

/*--------- leave group (server/client) ----------*/

/** \brief request for leaving a group */
typedef struct AooRequestGroupLeave
{
    AOO_REQUEST_HEADER
    AooId group;
} AooRequestGroupLeave;

/** \brief response for leaving a group */
typedef AooResponseBase AooResponseGroupLeave;

/*------------ update group metadata -------------*/

/** \brief request for updating a group */
typedef struct AooRequestGroupUpdate
{
    AOO_REQUEST_HEADER
    AooId groupId;
    AooData groupMetadata;
} AooRequestGroupUpdate;

/** \brief response for updating a group */
typedef struct AooResponseGroupUpdate
{
    AOO_RESPONSE_HEADER
    AooData groupMetadata;
} AooResponseGroupUpdate;

/*------------ update user metadata -------------*/

/** \brief request for updating a user */
typedef struct AooRequestUserUpdate
{
    AOO_REQUEST_HEADER
    AooId groupId;
    AooId userId;
    AooData userMetadata;
} AooRequestUserUpdate;

/** \brief response for updating a user */
typedef struct AooResponseUserUpdate
{
    AOO_RESPONSE_HEADER
    AooData userMetadata;
} AooResponseUserUpdate;

/*------- custom request (server/client) --------*/

/** \brief custom client request */
typedef struct AooRequestCustom
{
    AOO_REQUEST_HEADER
    AooData data;
    AooFlag flags; /* TODO: do we need this? */
} AooRequestCustom;

/** \brief custom server response */
typedef struct AooResponseCustom
{
    AOO_RESPONSE_HEADER
    AooData data;
    AooFlag flags; /* TODO: do we need this? */
} AooResponseCustom;

/*--------------------------------------------*/

/** \brief union of all client requests */
union AooRequest
{
    AooRequestType type; /** request type */
    AooRequestConnect connect;
    AooRequestDisconnect disconnect;
    AooRequestQuery query;
    AooRequestLogin login;
    AooRequestGroupJoin groupJoin;
    AooRequestGroupLeave groupLeave;
    AooRequestGroupUpdate groupUpdate;
    AooRequestUserUpdate userUpdate;
    AooRequestCustom custom;
};

/*--------------------------------------------*/

/** \brief union of all client responses */
union AooResponse
{
    AooRequestType type; /** request type */
    AooResponseError error;
    AooResponseConnect connect;
    AooResponseDisconnect disconnect;
    AooResponseQuery query;
    AooResponseLogin login;
    AooResponseGroupJoin groupJoin;
    AooResponseGroupLeave groupLeave;
    AooResponseGroupUpdate groupUpdate;
    AooResponseUserUpdate userUpdate;
    AooResponseCustom custom;
};

/*--------------------------------------------*/

AOO_PACK_END
