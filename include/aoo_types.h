/* Copyright (c) 2021 Christof Ressi
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

/** \file
 * \brief AOO types and structs
 */

#pragma once

#include "aoo_config.h"
#include "aoo_defines.h"

#include <stdint.h>
#include <stddef.h>

AOO_PACK_BEGIN

/*------------------------------------------------------------------*/

/** \brief boolean type */
typedef int32_t AooBool;

/** \brief 'true' boolean constant */
#define kAooTrue ((AooBool)1)
/** \brief 'false' boolean constant */
#define kAooFalse ((AooBool)0)

/** \brief character type */
typedef char AooChar;

/** \brief byte type */
typedef uint8_t AooByte;

/** \brief 16-bit signed integer */
typedef int16_t AooInt16;
/** \brief 16-bit unsigned integer */
typedef uint16_t AooUInt16;

/** \brief 32-bit signed integer */
typedef int32_t AooInt32;
/** \brief 32-bit unsigned integer */
typedef uint32_t AooUInt32;

/** \brief 64-bit signed integer */
typedef int64_t AooInt64;
/** \brief 64-bit unsigned integer */
typedef uint64_t AooUInt64;

/** \brief size type */
typedef size_t AooSize;

/** \brief pointer-sized signed integer */
typedef intptr_t AooIntPtr;
/** \brief pointer-sized unsigned integer */
typedef uintptr_t AooUIntPtr;

/*------------------------------------------------------------------*/

/** \brief struct size type */
typedef AooUInt32 AooStructSize;

/** \brief generic ID type */
typedef AooInt32 AooId;

/** \brief invalid AooId constant */
#define kAooIdInvalid -1
/** \brief alias for kAooIdInvalid */
#define kAooIdNone kAooIdInvalid
/** \brief alias for kAooIdInvalid */
#define kAooIdAll kAooIdInvalid
/** \brief smallest valid AooId */
#define kAooIdMin 0
/** \brief largest valid AooId */
#define kAooIdMax INT32_MAX

/** \brief fixed-width enum type */
typedef AooInt32 AooEnum;

/** \brief define a fixed-width named enum */
#define AOO_ENUM(name) \
    typedef AooEnum name; \
    enum

/** \brief flag/bitmap type */
typedef AooUInt32 AooFlag;

/** \brief define fixed-width named flags */
#define AOO_FLAG(name) \
    typedef AooFlag name; \
    enum

/*------------------------------------------------------------------*/

#if AOO_SAMPLE_SIZE == 32
/** \brief audio sample type */
typedef float AooSample;
#elif AOO_SAMPLE_SIZE == 64
/** \brief audio sample type */
typedef double AooSample;
#else
# error "unsupported value for AOO_SAMPLE_SIZE"
#endif

/** \brief NTP time point */
typedef AooUInt64 AooNtpTime;

/** \brief constant representing the current time */
#define kAooNtpTimeNow 1

/** \brief time point/interval in seconds */
typedef double AooSeconds;

/** \brief infinite duration */
#define kAooInfinite -1.0

/** \brief sample rate type */
typedef double AooSampleRate;

/** \brief AOO control type */
typedef AooInt32 AooCtl;

/*------------------------------------------------------------------*/

/** \brief socket address size type */
typedef AooUInt32 AooAddrSize;

/** \brief socket address */
typedef struct AooSockAddr
{
    /** pointer to sockaddr */
    const void *data;
    /** sockaddr size */
    AooAddrSize size;
} AooSockAddr;

/** \brief AOO endpoint */
typedef struct AooEndpoint
{
    /** socket address */
    const void *address;
    /** socket address length */
    AooAddrSize addrlen;
    /** source/sink ID */
    AooId id;
} AooEndpoint;

/** \brief AOO IP endpoint */
typedef struct AooIpEndpoint
{
    /** host name or IP string */
    const AooChar *hostName;
    /** port number */
    AooUInt16 port;
} AooIpEndpoint;

/*------------------------------------------------------------------*/

/** \brief AOO message types */
AOO_ENUM(AooMsgType)
{
    /** AOO source */
    kAooMsgTypeSource = 0,
    /** AOO sink */
    kAooMsgTypeSink,
    /** AOO server */
    kAooMsgTypeServer,
    /** AOO client */
    kAooMsgTypeClient,
    /** AOO peer */
    kAooMsgTypePeer,
    /** relayed message */
    kAooMsgTypeRelay
};

/*------------------------------------------------------------------*/

/** \brief error codes */
AOO_ENUM(AooError)
{
    /** unknown/unspecified error */
    kAooErrorUnspecified = -1,
    /** no error (= success) */
    kAooErrorNone = 0,
    /*------------- generic errors -------------*/
    /** operation/control not implemented */
    kAooErrorNotImplemented,
    /** operation/control not permitted */
    kAooErrorNotPermitted,
    /** not initialized */
    kAooErrorNotInitialized,
    /** bad argument for function/method call */
    kAooErrorBadArgument,
    /** bad message/structure format */
    kAooErrorBadFormat,
    /** argument(s) out of range */
    kAooErrorOutOfRange,
    /** AOO source/sink is idle;
     * no need to call `send()` resp. notify the send thread */
    kAooErrorIdle,
    /** operation would block */
    kAooErrorWouldBlock,
    /** operation would overflow */
    kAooErrorOverflow,
    /** operation timed out */
    kAooErrorTimeout,
    /** out of memory */
    kAooErrorOutOfMemory,
    /** resource already exists */
    kAooErrorAlreadyExists,
    /** resource not found */
    kAooErrorNotFound,
    /** insufficient buffer size */
    kAooErrorInsufficientBuffer,
    /** bad state */
    kAooErrorBadState,
    /** socket error
     * use errno resp. WSAGetLastError for more details */
    kAooErrorSocket,
    /** codec error */
    kAooErrorCodec,
    /** internal error */
    kAooErrorInternal,
    /** system error
     * use errno resp. GetLastError for more details */
    kAooErrorSystem,
    /** user-defined error */
    kAooErrorUserDefined,
    /*------------- server errors -------------*/
    /** request is already in progress */
    kAooErrorRequestInProgress = 1000,
    /** request has not been handled */
    kAooErrorUnhandledRequest,
    /** version not supported */
    kAooErrorVersionNotSupported,
    /** UDP handshake time out */
    kAooErrorUDPHandshakeTimeout,
    /** wrong passoword */
    kAooErrorWrongPassword,
    /** already connected to server */
    kAooErrorAlreadyConnected,
    /** not connected to server */
    kAooErrorNotConnected,
    /** group does not exist */
    kAooErrorGroupDoesNotExist,
    /** cannot create group on the server */
    kAooErrorCannotCreateGroup,
    /** already a group member */
    kAooErrorAlreadyGroupMember,
    /** not a group member */
    kAooErrorNotGroupMember,
    /** user already exists */
    kAooErrorUserAlreadyExists,
    /** user does not exist */
    kAooErrorUserDoesNotExist,
    /** cannot create user */
    kAooErrorCannotCreateUser,
    /** client or server not responding */
    kAooErrorNotResponding,
    /** start of user defined error codes (for custom AOO versions) */
    kAooErrorCustom = 10000
};

/** \brief alias for success result */
#define kAooOk kAooErrorNone

/*------------------------------------------------------------------*/

/** \brief flags for AooSource/AooSink setup */
AOO_FLAG(AooSetupFlags)
{
    /** the number of samples passed to the process() function
     * will always equal the block size. */
    kAooFixedBlockSize = 0x01,
    /** assume that the timestamps are sufficiently precise;
     * for example, your host or audio backend might be able
     * to provide accurate system time snapshots. */
    kAooPreciseTimestamp = 0x02
};

/*------------------------------------------------------------------*/

/** \brief resample modes for AooSource::setResampleMethod() and
 *  AooSink::setResampleMethod */
AOO_ENUM(AooResampleMethod)
{
    /** sample and hold */
    kAooResampleHold = 0,
    /** linear interpolation */
    kAooResampleLinear,
    /** cubic interpolation */
    kAooResampleCubic,
    /** sentinel */
    kAooResampleMethodEnd
};

/*------------------------------------------------------------------*/

/** \brief flags for AooClient::setup and AooServer::setup
 *
 * Possible combinations:
 *   - `kAooSocketIPv4` IPv4 only
 *   - `kAooSocketIPv6` IPv6 only
 *   - `kAooSocketIPv4 | kAooSocketIPv6` dedicated IPv4 and IPv6 sockets
 *   - `kAooSocketIP6 | kAooSocketIPv4Mapped` IPv6 dual-stack socket
 */
AOO_FLAG(AooSocketFlags)
{
    /** default settings */
    kAooSocketDefault = 0x00,
    /** client uses IPv4 addresses */
    kAooSocketIPv4 = 0x01,
    /** client uses IPv6 addresses */
    kAooSocketIPv6 = 0x02,
    /** client uses IPv4-mapped addresses */
    kAooSocketIPv4Mapped = 0x04
};

/** dual-stack socket */
#define kAooSocketDualStack (kAooSocketIPv6 | kAooSocketIPv4Mapped)

/*------------------------------------------------------------------*/

/** \brief see AooEventPeer */
AOO_FLAG(AooPeerFlags)
{
    /** peer-to-peer not possible, need relay */
    kAooPeerNeedRelay = 0x01,
    /** peer persists between sessions */
    kAooPeerPersistent = 0x02,
    /** peer has created the group */
    kAooPeerGroupCreator = 0x04
};

/*------------------------------------------------------------------*/

/** \brief see AooEventGroupAdd and AooResponseGroupJoin */
AOO_FLAG(AooGroupFlags)
{
    /** group persists between sessions */
    kAooGroupPersistent = 0x01
};

/*------------------------------------------------------------------*/

/** \brief see AooEventGroupJoin and AooResponseGroupJoin */
AOO_FLAG(AooUserFlags)
{
    /** user persists between sessions */
    kAooUserPersistent = 0x01,
    /** user has created the group */
    kAooUserGroupCreator = 0x02
};

/*------------------------------------------------------------------*/

/** \brief flags for AooClient_sendMessage / AooClient::sendMessage */
AOO_FLAG(AooMessageFlags)
{
    /** message should be delivered reliable */
    kAooMessageReliable = 0x01
};

/*------------------------------------------------------------------*/

/** \brief thread levels (`AooThreadLevel`) */
AOO_ENUM(AooThreadLevel)
{
    /** unknown thread level */
    kAooThreadLevelUnknown = 0,
    /** audio thread */
    kAooThreadLevelAudio = 1,
    /** network thread(s) */
    kAooThreadLevelNetwork = 2
};

/*------------------------------------------------------------------*/

/** \brief event modes */
AOO_ENUM(AooEventMode)
{
    /** no events */
    kAooEventModeNone = 0,
    /** use event callback */
    kAooEventModeCallback = 1,
    /** poll for events */
    kAooEventModePoll = 2
};

/*------------------------------------------------------------------*/

/** \cond DO_NOT_DOCUMENT */
typedef union AooEvent AooEvent;
/** \endcond */

/** \brief event handler function
 *
 * The callback function type that is passed to
 * AooSource, AooSink or AooClient to receive events.
 * If the callback is registered with #kAooEventModeCallback,
 * the function is called immediately when an event occurs.
 * The event should be handled appropriately regarding the
 * current thread level.
 * If the callback is registered with #kAooEventModePoll,
 * the user has to regularly poll for pending events.
 * Polling itself is realtime safe and can be done from
 * any thread.
 */
typedef void (AOO_CALL *AooEventHandler)(
        /** the user data */
        void *user,
        /** the event */
        const AooEvent *event,
        /** the current thread level
         * (only releveant for #kAooEventModeCallback) */
        AooThreadLevel level
);

/*------------------------------------------------------------------*/

/** \brief UDP send function
 *
 * The function type that is passed to #AooSource,
 * #AooSink or #AooClient for sending outgoing network packets.
 */
typedef AooInt32 (AOO_CALL *AooSendFunc)(
        /** the user data */
        void *user,
        /** the packet content */
        const AooByte *data,
        /** the packet size in bytes */
        AooInt32 size,
        /** the socket address */
        const void *address,
        /** the socket address length */
        AooAddrSize addrlen,
        /** optional flags */
        /* TODO do we need this? */
        AooFlag flags
);

/*------------------------------------------------------------------*/

/** \brief UDP receive function
 *
 * Used in #AooSettings.
 * \return #kAooOk if the message could be handled,
 * or an error otherwise.
 */
typedef AooError (AOO_CALL *AooReceiveFunc)(
    /** the user data */
    void *user,
    /** the packet content */
    const AooByte *data,
    /** the packet size in bytes */
    AooInt32 size,
    /** the source socket address */
    const void *address,
    /** the source socket address length */
    AooAddrSize addrlen
);

/*------------------------------------------------------------------*/

/** \brief AOO data types */
AOO_ENUM(AooDataType)
{
    /** unspecified data type */
    kAooDataUnspecified = -1,
    /** raw or binary data */
    kAooDataRaw = 0,
    kAooDataBinary = 0,
    /** plain text (UTF-8 encoded) */
    kAooDataText,
    /** OSC message (Open Sound Control) */
    kAooDataOSC,
    /** MIDI */
    kAooDataMIDI,
    /** FUDI (Pure Data) */
    kAooDataFUDI,
    /** JSON (UTF-8 encoded) */
    kAooDataJSON,
    /** XML (UTF-8 encoded) */
    kAooDataXML,
    /** 32-bit float array (big-endian!) */
    kAooDataFloat32,
    /** 64-bit float array (big-endian!) */
    kAooDataFloat64,
    /** 16-bit int array (big-endian!) */
    kAooDataInt16,
    /** 32-bit int array (big-endian!) */
    kAooDataInt32,
    /** 64-bit int array (big-endian!) */
    kAooDataInt64,
    /** start of user specified types */
    kAooDataUser = 1000
};

/*------------------------------------------------------------------*/

/** \brief view on arbitrary structured data */
typedef struct AooData
{
    /** the data type */
    AooDataType type;
    /** the data content */
    const AooByte *data;
    /** the data size in bytes */
    AooSize size;
} AooData;

/*------------------------------------------------------------------*/

/** \brief AOO stream message */
typedef struct AooStreamMessage
{
    /** sample offset */
    AooInt32 sampleOffset;
    /** channel number */
    AooInt32 channel;
    /** the message type */
    AooDataType type;
    /** the data size in bytes */
    AooInt32 size;
    /** the data content */
    const AooByte *data;
} AooStreamMessage;

/** max. stream message size */
#define kAooStreamMessageMaxSize 65535

/*------------------------------------------------------------------*/

/** \brief stream message handler
 *
 * The type of function that is passed to #AooSink::process
 * for handling stream messages.
 *
 * \warning Do not call any AOO functions inside the handler function!
 */
typedef void (AOO_CALL *AooStreamMessageHandler)(
        /** the user data */
        void *user,
        /** the stream message */
        const AooStreamMessage *message,
        /** the AOO source that sent the message */
        const AooEndpoint *source
);

/*------------------------------------------------------------------*/

/** \brief max. size of codec names (including \0) */
#define kAooCodecNameMaxSize 32

/** \brief common header for all AOO format structs */
typedef struct AooFormat
{
    /** the format structure size (including the header) */
    AooInt32 structSize;
    /** the number of channels */
    AooInt32 numChannels;
    /** the sample rate */
    AooInt32 sampleRate;
    /** the max. block size */
    AooInt32 blockSize;
    /** the codec name */
    AooChar codecName[kAooCodecNameMaxSize];
} AooFormat;

/*------------------------------------------------------------------*/

/** \brief the max. size of an AOO format struct */
#define kAooFormatMaxSize 128
/** \brief the max. size of AOO format header extensions */
#define kAooFormatExtMaxSize (kAooFormatMaxSize - sizeof(AooFormat))

/** \brief helper struct large enough to hold any AOO format struct */
typedef struct AooFormatStorage
{
    /** format header */
    AooFormat header;
    /** opaque data */
    AooByte data[kAooFormatExtMaxSize];
} AooFormatStorage;

/*------------------------------------------------------------------*/

/** \brief options for AooClientSettings */
AOO_FLAG(AooClientOptions)
{
    /** use external UDP socket */
    kAooClientExternalUDPSocket = 0x01
};

/** \brief settings for AooClient::setup() */
typedef struct AooClientSettings
{
#ifdef __cplusplus
    /** default constructor */
    AooClientSettings()
        : structSize(AOO_STRUCT_SIZE(AooClientSettings, messageHandler)),
          options(0), portNumber(0), socketType(kAooSocketDefault),
          userData(NULL), sendFunc(NULL), messageHandler(NULL) {}
#endif

    /** struct size */
    AooSize structSize;
    /** option flags */
    AooClientOptions options;
    /** the listening port of the UDP socket; if set to 0, AooClient will
     *  pick a free port and update this field accordingly.
     *  If you use an external UDP socket, you must specify the port. */
    AooUInt16 portNumber;
    /** socket type; by default, it will try to create a dual-stack socket.
     *  If you use an external UDP socket, you must specify the socket type. */
    AooSocketFlags socketType;
    /** (optional) user data passed to callback functions, e.g. sendFunc
     * or messageHandler. */
    void *userData;
    /** (optional) UDP send function; only for external UDP socket */
    AooSendFunc sendFunc;
    /** (optional) default handler for non-AOO messages; typically used
     * to implement message "side-channels" */
    AooReceiveFunc messageHandler;
} AooClientSettings;

/** \brief (C only) default initializer for AooClientSettings struct */
#define AOO_CLIENT_SETTINGS_INIT() \
    { AOO_STRUCT_SIZE(AooClientSettings, messageHandler), 0, 0, \
        kAooSocketDefault, NULL, NULL, NULL }

/*------------------------------------------------------------------*/

/** \brief arguments for AooClient::connect() method */
typedef struct AooClientConnect {
#ifdef __cplusplus
    AooClientConnect()
        : structSize(AOO_STRUCT_SIZE(AooClientConnect, timeout)),
          hostName(NULL), port(0), password(NULL), metadata(NULL),
          timeout(0) {}
#endif

    /** struct size */
    AooSize structSize;
    /** the AOO server host name */
    const AooChar *hostName;
    /** port the AOO server port */
    AooUInt16 port;
    /** password (optional) password */
    const AooChar *password;
    /** (optional) metadata */
    const AooData *metadata;
    /** (optional) connection timeout; possible values:
     *  kAooInfinite (= no timeout), 0 (= default timeout),
     *  0< (= timeout in seconds */
    AooSeconds timeout;
} AooClientConnect;

/** \brief (C only) default initializer for AooClientConnect struct */
#define AOO_CLIENT_CONNECT_INIT() \
    { AOO_STRUCT_SIZE(AooClientConnect, timeout), \
        NULL, 0, NULL, NULL, 0 }

/*------------------------------------------------------------------*/

/** \brief arguments for AooClient::groupJoin() method */
typedef struct AooClientJoinGroup {
#ifdef __cplusplus
    /** default constructor */
    AooClientJoinGroup()
        : structSize(AOO_STRUCT_SIZE(AooClientJoinGroup, relayAddress)),
          groupName(NULL), groupPassword(NULL), groupMetadata(NULL),
          userName(NULL), userPassword(NULL), userMetadata(NULL),
          relayAddress(NULL) {}
#endif

    /** struct size */
    AooSize structSize;
    /** group name */
    const AooChar *groupName;
    /** (optional) group password */
    const AooChar *groupPassword;
    /** (optional) group metadata
         *  See AooResponseGroupJoin::groupMetadata. */
    const AooData *groupMetadata;
    /** user name */
    const AooChar *userName;
    /** (optional) user password */
    const AooChar *userPassword;
    /** (optional) user metadata
         *  See AooResponseGroupJoin::userMetadata resp.
         *  AooEventPeer::metadata. */
    const AooData *userMetadata;
    /** (optional) relay address
         *  If `hostName` is `NULL`, it means that the relay
         *  has the same IP address(es) as the AOO client. */
    const AooIpEndpoint *relayAddress;
} AooClientJoinGroup;

/** \brief (C only) default initializer for AooClientJoinGroup struct */
#define AOO_CLIENT_JOIN_GROUP_INIT() \
    { AOO_STRUCT_SIZE(AooClientJoinGroup, relayAddress), \
        NULL, NULL, NULL, NULL, NULL, NULL, NULL }

/*------------------------------------------------------------------*/

/** \brief options for AooServerSettings */
AOO_FLAG(AooServerOptions)
{
    /** use external UDP socket */
    kAooServerExternalUDPSocket = 0x01
};

/** \brief settings for AooClient::setup() */
typedef struct AooServerSettings
{
#ifdef __cplusplus
    /** default constructor */
    AooServerSettings()
        : structSize(AOO_STRUCT_SIZE(AooServerSettings, sendFunc)),
          options(0), portNumber(0), socketType(kAooSocketDefault),
          userData(NULL), sendFunc(NULL) {}
#endif

    /** struct size */
    AooSize structSize;
    /** option flags */
    AooServerOptions options;
    /** the listening port of the UDP and TCP socket */
    AooUInt16 portNumber;
    /** socket type; by default it will try to create a dual-stack socket.
     *  If you use an external UDP socket, you must specify the socket type. */
    AooSocketFlags socketType;
    /** (optional) user data for callback functions, e.g. sendFunc */
    void *userData;
    /** (optional) send function for external UDP socket */
    AooSendFunc sendFunc;
} AooServerSettings;

/** \brief (C only) default initializer for AooServerSettings struct */
#define AOO_SERVER_SETTINGS_INIT() \
    { AOO_STRUCT_SIZE(AooServerSettings, sendFunc), 0, 0, \
        kAooSocketDefault, NULL, NULL }

/*------------------------------------------------------------------*/

/** \brief ping settings */
typedef struct AooPingSettings
{
    /** regular ping interval */
    AooSeconds interval;
    /** time to wait before probing */
    AooSeconds probeTime;
    /** probe ping interval */
    AooSeconds probeInterval;
    /** max. number of probe pings */
    AooInt32 probeCount;
} AooPingSettings;

/*------------------------------------------------------------------*/

/** \cond DO_NOT_DOCUMENT */
typedef union AooRequest AooRequest;
/** \endcond */

/** \brief server request handler (to intercept client requests)
 * \param user user data
 * \param client client ID
 * \param token request token
 * \param request the client request
 * \return #kAooTrue: handled manually; #kAooFalse: handle automatically.
 */
typedef AooBool (AOO_CALL *AooRequestHandler)(
        void *user,
        AooId client,
        AooId token,
        const AooRequest *request
);

/*------------------------------------------------------------------*/

/** \cond DO_NOT_DOCUMENT */
typedef union AooResponse AooResponse;
/** \endcond */

/** \brief client response handler
 *
 * If `result` is `kAooErrorNone`, you can safely access the corresponding
 * response member, e.g. `response->connect` for a connection request.
 * Otherwise the request has failed. If the error code is `kAooErrorSystem`
 * or `kAooErrorUserDefined`, you may access `response->error` for more details.
 */
typedef void (AOO_CALL *AooResponseHandler)(
        /** the user data */
        void *user,
        /** the original request */
        const AooRequest *request,
        /** the result error code */
        AooError result,
        /** the response */
        const AooResponse *response
);

/*------------------------------------------------------------------*/

/** \brief custom allocator function
 * \param ptr pointer to memory block;
 *    `NULL` if `oldsize` is 0.
 * \param oldsize original size of memory block;
 *    0 for allocating new memory
 * \param newsize size of the memory block;
 *    0 for freeing existing memory.
 * \return pointer to the new memory block;
 *    `NULL` if `newsize` is 0 or the allocation failed.
 *
 * If `oldsize` is 0 and `newsize` is not 0, the function
 * shall behave like `malloc`.
 * If `oldsize` is not 0 and `newsize` is 0, the function
 * shall behave like `free`.
 * If both `oldsize`and `newsize` are not 0, the function
 * shall behave like `realloc`.
 * If both `oldsize` and `newsize` are 0, the function
 * shall have no effect.
 */
typedef void * (AOO_CALL *AooAllocFunc)
    (void *ptr, AooSize oldsize, AooSize newsize);

/*------------------------------------------------------------------*/

/** \brief log levels */
typedef AooInt32 AooLogLevel;

/* NB: log level constants must be macros, otherwise they cannot
 * be used in #if clauses as they would expand to zero! */

/** \brief no logging */
#define kAooLogLevelSilent 0
/** \brief only log errors */
#define kAooLogLevelError 1
/** \brief log errors and warnings */
#define kAooLogLevelWarning 2
/** \brief log errors, warnings and info messages */
#define kAooLogLevelInfo 3
/** \brief log errors, warnings, info and debug messages */
#define kAooLogLevelDebug 4
/** \brief extra verbose logging */
#define kAooLogLevelVerbose 5

/** \brief custom log function type
 * \param level the log level
 * \param message the message
 */
typedef void (AOO_CALL *AooLogFunc)
        (AooLogLevel level, const AooChar *message);

/*------------------------------------------------------------------*/

AOO_PACK_END
