#include "Aoo.hpp"

#include "aoo/codec/aoo_pcm.h"
#if USE_CODEC_OPUS
#include "aoo/codec/aoo_opus.h"
#endif

#include "common/time.hpp"

#include <unordered_map>
#include <vector>

static InterfaceTable *ft;

namespace rt {
    InterfaceTable* interfaceTable;
}

// TODO: honor Server verbosity
static void SCLog(AooLogLevel level, const char *s, ...) {
    if (level == kAooLogLevelError) {
        Print("ERROR: %s", s);
    } else if (level == kAooLogLevelWarning) {
        Print("WARNING: %s", s);
    } else {
        Print("%s", s);
    }
}

/*////////////////// Replies //////////////////////*/

namespace {

using shared_lock = aoo::sync::shared_lock<aoo::sync::shared_mutex>;
using unique_lock = aoo::sync::unique_lock<aoo::sync::shared_mutex>;

int gClientSocket = -1;
aoo::ip_address::ip_type gClientSocketType = aoo::ip_address::Unspec;
aoo::sync::mutex gClientSocketMutex;

using ClientList = std::vector<aoo::ip_address>;
std::unordered_map<World *, ClientList> gClientMap;
aoo::sync::shared_mutex gClientMutex;

struct ClientCmd {
    int id;
    int port;
    char host[1];
};

bool registerClient(World *world, void *cmdData){
    LOG_DEBUG("register client");
    auto data = (ClientCmd *)cmdData;

    aoo::ip_address addr(data->host, data->port, gClientSocketType);

    unique_lock lock(gClientMutex);

    auto& clientList = gClientMap[world];
    bool found = false;
    for (auto& client : clientList){
        if (client == addr){
            LOG_WARNING("aoo: client already registered!");
            found = true;
            break;
        }
    }
    if (!found){
        clientList.push_back(addr);
    }

    char buf[256];
    osc::OutboundPacketStream msg(buf, 256);
    msg << osc::BeginMessage("/aoo/register") << data->id << osc::EndMessage;

    LOG_DEBUG("send client reply");
    aoo::socket_sendto(gClientSocket, msg.Data(), msg.Size(), addr);

    return true;
}

bool unregisterClient(World *world, void *cmdData){
    auto data = (ClientCmd *)cmdData;

    // sclang is IPv4 only
    aoo::ip_address addr(data->host, data->port, gClientSocketType);

    unique_lock lock(gClientMutex);

    auto& clientList = gClientMap[world];
    for (auto it = clientList.begin(); it != clientList.end(); ++it){
        if (*it == addr){
            clientList.erase(it);
            return true;
        }
    }

    LOG_WARNING("aoo: couldn't unregister client - not found!");

    return false;
}

void aoo_register(World* world, void* user,
                  sc_msg_iter* args, void* replyAddr)
{
    auto host = args->gets("");
    auto port = args->geti();
    auto id = args->geti();

    auto len = strlen(host) + 1;

    auto cmdData = CmdData::create<ClientCmd>(world, len);
    if (cmdData){
        cmdData->id = id;
        cmdData->port = port;
        memcpy(cmdData->host, host, len);

        DoAsynchronousCommand(world, replyAddr, "/aoo_register",
                              cmdData, registerClient, 0, 0,
                              CmdData::free<ClientCmd>, 0, 0);
    }
}

void aoo_unregister(World* world, void* user,
                  sc_msg_iter* args, void* replyAddr)
{
    auto host = args->gets("");
    auto port = args->geti();

    auto len = strlen(host) + 1;

    auto cmdData = CmdData::create<ClientCmd>(world, len);
    if (cmdData){
        cmdData->port = port;
        memcpy(cmdData->host, host, len);

        DoAsynchronousCommand(world, replyAddr, "/aoo_unregister",
                              cmdData, unregisterClient, 0, 0,
                              CmdData::free<ClientCmd>, 0, 0);
    }
}

struct OscMsgCommand {
    size_t size;
    char data[1];
};

} // namespace

void sendMsgNRT(World *world, const char *data, int32_t size){
    shared_lock lock(gClientMutex);
    for (auto& addr : gClientMap[world]){
        // sendMsgNRT can be called from different threads
        // (NRT thread and network receive thread)
        aoo::sync::scoped_lock<aoo::sync::mutex> l(gClientSocketMutex);
        aoo::socket_sendto(gClientSocket, data, size, addr);
    }
}

void sendMsgRT(World *world, const char *data, int32_t size){
    auto cmd = CmdData::create<OscMsgCommand>(world, size);
    if (cmd){
        cmd->size = size;
        memcpy(cmd->data, data, size);

        auto sendMsg = [](World *world, void *cmdData){
            auto cmd = (OscMsgCommand *)cmdData;

            sendMsgNRT(world, cmd->data, cmd->size);

            return false; // done
        };

        DoAsynchronousCommand(world, 0, 0, cmd, sendMsg, 0, 0, &RTFree, 0, 0);
    } else {
        LOG_ERROR("RTAlloc() failed!");
    }
}

/*////////////////// Commands /////////////////////*/

// Check if the Unit is still alive. Should only be called in RT stages!
bool CmdData::alive() const {
    auto b = owner->alive();
    if (!b) {
        LOG_WARNING("AooUnit: freed during background task");
    }
    return b;
}

void* CmdData::doCreate(World *world, int size) {
    auto data = RTAlloc(world, size);
    if (!data){
        LOG_ERROR("RTAlloc failed!");
    }
    return data;
}

void CmdData::doFree(World *world, void *cmdData){
    RTFree(world, cmdData);
    // LOG_DEBUG("cmdRTfree!");
}

UnitCmd *UnitCmd::create(World *world, sc_msg_iter *args){
    auto data = (UnitCmd *)RTAlloc(world, sizeof(UnitCmd) + args->size);
    if (!data){
        LOG_ERROR("RTAlloc failed!");
        return nullptr;
    }
    new (data) UnitCmd(); // !

    data->size = args->size;
    memcpy(data->data, args->data, args->size);

    return data;
}

/*////////////////// AooDelegate ///////////////*/

AooDelegate::AooDelegate(AooUnit& owner)
    : world_(owner.mWorld), owner_(&owner)
{
    LOG_DEBUG("AooDelegate");
}

AooDelegate::~AooDelegate() {
    LOG_DEBUG("~AooDelegate");
}

void AooDelegate::doCmd(CmdData *cmdData, AsyncStageFn stage2,
    AsyncStageFn stage3, AsyncStageFn stage4, AsyncFreeFn cleanup) {
    // so we don't have to always check the return value of makeCmdData
    if (cmdData) {
        cmdData->owner = shared_from_this();
        DoAsynchronousCommand(world_, 0, 0, cmdData,
                              stage2, stage3, stage4, cleanup, 0, 0);
    }
}

void AooDelegate::beginReply(osc::OutboundPacketStream &msg, const char *cmd, int replyID){
    msg << osc::BeginMessage(cmd) << owner_->mParent->mNode.mID << owner_->mParentIndex << replyID;
}

void AooDelegate::beginEvent(osc::OutboundPacketStream &msg, const char *event)
{
    msg << osc::BeginMessage("/aoo/event")
        << owner_->mParent->mNode.mID << owner_->mParentIndex << event;
}

void AooDelegate::beginEvent(osc::OutboundPacketStream &msg, const char *event,
                             const aoo::ip_address& addr, AooId id)
{
    msg << osc::BeginMessage("/aoo/event")
        << owner_->mParent->mNode.mID << owner_->mParentIndex
        << event << addr.name() << addr.port() << id;
}

void AooDelegate::sendMsgRT(osc::OutboundPacketStream &msg){
    msg << osc::EndMessage;
    ::sendMsgRT(world_, msg);
}

void AooDelegate::sendMsgNRT(osc::OutboundPacketStream &msg){
    msg << osc::EndMessage;
    ::sendMsgNRT(world_, msg);
}

/*////////////////// Helper functions ///////////////*/

// Try to update OSC time only once per DSP cycle.
// We use thread local variables instead of a dictionary
// (which we would have to protect with a mutex).
// This is certainly fine for the case of a single World,
// and it should be more or less ok for libscsynth.
uint64_t getOSCTime(World *world){
    thread_local AooNtpTime time;
    thread_local int lastBuffer{-1};

    if (lastBuffer != world->mBufCounter){
        time = aoo_getCurrentNtpTime();
        lastBuffer = world->mBufCounter;
    }
    return time;
}

void makeDefaultFormat(AooFormatStorage& f, int sampleRate,
                       int blockSize, int numChannels)
{
    auto& fmt = (AooFormatPcm &)f;
    fmt.header.codec = kAooCodecPcm;
    fmt.header.size = sizeof(AooFormatPcm);
    fmt.header.blockSize = blockSize;
    fmt.header.sampleRate = sampleRate;
    fmt.header.numChannels = numChannels;
    fmt.bitDepth = kAooPcmFloat32;
}

static int32_t getFormatParam(sc_msg_iter *args, const char *name, int32_t def)
{
    if (args->remain() > 0){
        if (args->nextTag() == 's'){
            auto s = args->gets();
            if (strcmp(s, "auto")){
                LOG_ERROR("aoo: bad " << name << " argument " << s
                          << ", using " << def);
            }
        } else {
            return args->geti();
        }
    }
    return def;
}

bool parseFormat(const AooUnit& unit, int defNumChannels,
                 sc_msg_iter *args, AooFormatStorage &f)
{
    const char *codec = args->gets("");

    if (!strcmp(codec, kAooCodecPcm)){
        auto& fmt = (AooFormatPcm &)f;
        fmt.header.codec = kAooCodecPcm;
        fmt.header.size = sizeof(AooFormatPcm);
        fmt.header.numChannels = getFormatParam(args, "channels", defNumChannels);
        fmt.header.blockSize = getFormatParam(args, "blocksize", unit.bufferSize());
        fmt.header.sampleRate = getFormatParam(args, "samplerate", unit.sampleRate());

        int bitdepth = getFormatParam(args, "bitdepth", 4);
        switch (bitdepth){
        case 2:
            fmt.bitDepth = kAooPcmInt16;
            break;
        case 3:
            fmt.bitDepth = kAooPcmInt24;
            break;
        case 4:
            fmt.bitDepth = kAooPcmFloat32;
            break;
        case 8:
            fmt.bitDepth = kAooPcmFloat64;
            break;
        default:
            LOG_ERROR("aoo: bad bitdepth argument " << bitdepth);
            return false;
        }
    }
#if USE_CODEC_OPUS
    else if (!strcmp(codec, kAooCodecOpus)){
        auto &fmt = (AooFormatOpus &)f;
        fmt.header.codec = kAooCodecOpus;
        fmt.header.size = sizeof(AooFormatOpus);
        fmt.header.numChannels = getFormatParam(args, "channels", defNumChannels);
        fmt.header.blockSize = getFormatParam(args, "blocksize", 480); // 10ms
        fmt.header.sampleRate = getFormatParam(args, "samplerate", 48000);

        // bitrate ("auto", "max" or float)
        if (args->remain() > 0){
            if (args->nextTag() == 's'){
                auto s = args->gets();
                if (!strcmp(s, "auto")){
                    fmt.bitrate = OPUS_AUTO;
                } else if (!strcmp(s, "max")){
                    fmt.bitrate = OPUS_BITRATE_MAX;
                } else {
                    LOG_ERROR("aoo: bad bitrate argument '" << s << "'");
                    return false;
                }
            } else {
                int bitrate = args->geti();
                if (bitrate > 0){
                    fmt.bitrate = bitrate;
                } else {
                    LOG_ERROR("aoo: bitrate argument " << bitrate
                              << " out of range");
                    return false;
                }
            }
        } else {
            fmt.bitrate = OPUS_AUTO;
        }
        // complexity ("auto" or 0-10)
        int complexity = getFormatParam(args, "complexity", OPUS_AUTO);
        if ((complexity < 0 || complexity > 10) && complexity != OPUS_AUTO){
            LOG_ERROR("aoo: complexity value " << complexity << " out of range");
            return false;
        }
        fmt.complexity = complexity;
        // signal type ("auto", "music", "voice")
        if (args->remain() > 0){
            auto type = args->gets("");
            if (!strcmp(type, "auto")){
                fmt.signalType = OPUS_AUTO;
            } else if (!strcmp(type, "music")){
                fmt.signalType = OPUS_SIGNAL_MUSIC;
            } else if (!strcmp(type, "voice")){
                fmt.signalType = OPUS_SIGNAL_VOICE;
            } else {
                LOG_ERROR("aoo: unsupported signal type '" << type << "'");
                return false;
            }
        } else {
            fmt.signalType = OPUS_AUTO;
        }
        // application type ("auto", "audio", "voip", "lowdelay")
        if (args->remain() > 0){
            auto type = args->gets("");
            if (!strcmp(type, "auto") || !strcmp(type, "audio")){
                fmt.applicationType = OPUS_APPLICATION_AUDIO;
            } else if (!strcmp(type, "voip")){
                fmt.applicationType = OPUS_APPLICATION_VOIP;
            } else if (!strcmp(type, "lowdelay")){
                fmt.applicationType = OPUS_APPLICATION_RESTRICTED_LOWDELAY;
            } else {
                LOG_ERROR("aoo: unsupported application type '" << type << "'");
                return false;
            }
        } else {
            fmt.applicationType = OPUS_APPLICATION_AUDIO;
        }
    }
#endif
    else {
        LOG_ERROR("aoo: unknown codec '" << codec << "'");
        return false;
    }
    return true;
}

void serializeFormat(osc::OutboundPacketStream& msg, const AooFormat& f)
{
    msg << f.codec << (int32_t)f.numChannels
        << (int32_t)f.blockSize << (int32_t)f.sampleRate;

    if (!strcmp(f.codec, kAooCodecPcm)){
        // pcm <blocksize> <samplerate> <bitdepth>
        auto& fmt = (AooFormatPcm &)f;
        int nbits;
        switch (fmt.bitDepth){
        case kAooPcmInt16:
            nbits = 2;
            break;
        case kAooPcmInt24:
            nbits = 3;
            break;
        case kAooPcmFloat32:
            nbits = 4;
            break;
        case kAooPcmFloat64:
            nbits = 8;
            break;
        default:
            nbits = 0;
        }
        msg << nbits;
    }
#if USE_CODEC_OPUS
    else if (!strcmp(f.codec, kAooCodecOpus)){
        // opus <blocksize> <samplerate> <bitrate> <complexity> <signaltype> <application>
        auto& fmt = (AooFormatOpus &)f;
        // bitrate
    #if 0
        msg << fmt.bitrate;
    #else
        // workaround for bug in opus_multistream_encoder (as of opus v1.3.2)
        // where OPUS_GET_BITRATE would always return OPUS_AUTO.
        // We have no chance to get the actual bitrate for "auto" and "max",
        // so we return the symbols instead.
        switch (fmt.bitrate){
        case OPUS_AUTO:
            msg << "auto";
            break;
        case OPUS_BITRATE_MAX:
            msg << "max";
            break;
        default:
            msg << fmt.bitrate;
            break;
        }
    #endif
        // complexity
        msg << fmt.complexity;
        // signal type
        switch (fmt.signalType){
        case OPUS_SIGNAL_MUSIC:
            msg << "music";
            break;
        case OPUS_SIGNAL_VOICE:
            msg << "voice";
            break;
        default:
            msg << "auto";
            break;
        }
        // application type
        switch (fmt.applicationType){
        case OPUS_APPLICATION_VOIP:
            msg << "voip";
            break;
        case OPUS_APPLICATION_RESTRICTED_LOWDELAY:
            msg << "lowdelay";
            break;
        default:
            msg << "audio";
            break;
        }
    }
#endif
    else {
        LOG_ERROR("aoo: unknown codec " << f.codec);
    }
}

/*////////////////// Setup /////////////////////*/

void AooSendLoad(InterfaceTable *);
void AooReceiveLoad(InterfaceTable *);
void AooClientLoad(InterfaceTable *);
void AooServerLoad(InterfaceTable *);

PluginLoad(Aoo) {
    // InterfaceTable *inTable implicitly given as argument to the load function
    ft = inTable; // store pointer to InterfaceTable
    rt::interfaceTable = inTable; // for "rt_shared_ptr.h"

    aoo_initializeEx(SCLog, nullptr),

    Print("AOO (audio over OSC) %s\n", aoo_getVersionString());
    Print("  (c) 2020 Christof Ressi, Winfried Ritsch, et al.\n");

    std::string msg;
    if (aoo::check_ntp_server(msg)){
        Print("%s\n", msg.c_str());
    } else {
        Print("ERROR: %s\n", msg.c_str());
    }
    Print("\n");

    AooSendLoad(ft);
    AooReceiveLoad(ft);
    AooClientLoad(ft);
    AooServerLoad(ft);

    AooPluginCmd(aoo_register);
    AooPluginCmd(aoo_unregister);

    gClientSocket = aoo::socket_udp(0);
    if (gClientSocket >= 0) {
        gClientSocketType = aoo::socket_family(gClientSocket);
    }
    else {
        LOG_ERROR("AOO: couldn't open client socket - "
            << aoo::socket_strerror(aoo::socket_errno()));
    }
}
