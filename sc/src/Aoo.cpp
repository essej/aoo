#include "Aoo.hpp"

#include "codec/aoo_null.h"
#include "codec/aoo_pcm.h"
#if AOO_USE_OPUS
#include "codec/aoo_opus.h"
#endif

#include <unordered_map>
#include <vector>

namespace {

InterfaceTable *ft;

aoo::sync::mutex gClientSocketMutex;
aoo::udp_socket gClientSocket;
int gClientPort;
aoo::ip_address::ip_type gClientSocketType;
std::unordered_map<int, aoo::ip_address> gReplyAddrMap;

// Called from NRT threads(s)
bool registerClient(int port, const aoo::ip_address& addr) {
    std::lock_guard lock(gClientSocketMutex);
    if (auto it = gReplyAddrMap.find(port); it != gReplyAddrMap.end()) {
        if (it->second == addr) {
#if 0
            LOG_WARNING("aoo: client " << addr << " for port " << port
                         << " already registered!");
#endif
            return true;
        } else {
            LOG_ERROR("aoo: port " << port
                      << " already registered by client " << it->second);
            return false;
        }
    } else {
        gReplyAddrMap[port] = addr;
        return true;
    }
}

// Called from NRT thread(s)
bool unregisterClient(int port, const aoo::ip_address& addr) {
    std::lock_guard lock(gClientSocketMutex);
    if (auto it = gReplyAddrMap.find(port); it != gReplyAddrMap.end()) {
        if (it->second == addr) {
            gReplyAddrMap.erase(it);
            return true;
        } else {
            LOG_ERROR("aoo: cannot unregister client " << addr << " for port "
                      << port << " - wrong client (" << it->second << ")");
            return false;
        }
    } else {
        LOG_ERROR("aoo: cannot unregister client " << addr
                  << " for port " << port << " - port not found!");
        return false;
    }
}

// TODO: honor Server verbosity
void AOO_CALL SCLog(AooLogLevel level, const char *msg) {
    switch (level) {
    case kAooLogLevelError:
        Print("ERROR: %s\n", msg);
        break;
    case kAooLogLevelWarning:
        Print("WARNING: %s\n", msg);
        break;
    case kAooLogLevelInfo:
        Print("%s\n", msg);
        break;
    case kAooLogLevelDebug:
        Print("DEBUG: %s\n", msg);
        break;
    default:
        Print("VERBOSE: %s\n", msg);
        break;
    }
}

}

/*/////////////////// Reply ///////////////////////*/

aoo::ip_address findReplyAddr(int port) {
    std::lock_guard lock(gClientSocketMutex);
    if (auto it = gReplyAddrMap.find(port); it != gReplyAddrMap.end()) {
        return it->second;
    } else {
        return aoo::ip_address{};
    }
}

void sendReply(int port, const char *data, size_t size) {
    std::lock_guard lock(gClientSocketMutex);
    try {
        if (auto it = gReplyAddrMap.find(port); it != gReplyAddrMap.end()) {
            gClientSocket.send(data, size, it->second);
        } else {
            LOG_ERROR("aoo: could not send reply: could not find address");
        }
    } catch (const aoo::socket_error& e) {
        LOG_ERROR("aoo: could not send reply: " << e.what());
    }
}

void sendReply(const aoo::ip_address& addr, const osc::OutboundPacketStream& msg) {
    std::lock_guard lock(gClientSocketMutex); // lock!
    try {
        gClientSocket.send(msg.Data(), msg.Size(), addr);
    } catch (const aoo::socket_error& e) {
        LOG_ERROR("aoo: could not send reply: " << e.what());
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

osc::OutboundPacketStream& AooDelegate::beginReply(
    osc::OutboundPacketStream& msg, const char *cmd) {
    msg << osc::BeginMessage(cmd) << owner_->mParent->mNode.mID << owner_->mParentIndex;
    return msg;
}

void AooDelegate::sendMsgRT(osc::OutboundPacketStream &msg) {
    struct OscMsgCommand {
        int port;
        int size;
        char data[1];
    };

    msg << osc::EndMessage;

    auto cmd = (OscMsgCommand*)RTAlloc(world_, sizeof(OscMsgCommand) + msg.Size());
    if (!cmd) {
        LOG_ERROR("RTAlloc() failed!");
        return;
    }
    cmd->port = node_->port();
    cmd->size = msg.Size();
    memcpy(cmd->data, msg.Data(), msg.Size());

    auto sendfn = [](FifoMsg* msg) {
        auto cmd = (OscMsgCommand*)msg->mData;
        sendReply(cmd->port, cmd->data, cmd->size);
    };

    auto freefn = [](FifoMsg* msg) {
        msg->mWorld->ft->fRTFree(msg->mWorld, msg->mData);
    };

    FifoMsg fifoMsg;
    fifoMsg.Set(world_, sendfn, freefn, cmd);

    SendMsgFromRT(world_, fifoMsg);
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

/*////////////////// Helper functions ///////////////*/

// Try to update OSC time only once per DSP cycle.
// We use thread local variables instead of a dictionary
// (which we would have to protect with a mutex).
// This is certainly fine for the case of a single World,
// and it should be more or less ok for libscsynth.
uint64_t getOSCTime(World *world){
    thread_local AooNtpTime time;
    thread_local int lastBuffer = -1;

    if (lastBuffer != world->mBufCounter){
        time = aoo_getCurrentNtpTime();
        lastBuffer = world->mBufCounter;
    }
    return time;
}

void makeDefaultFormat(AooFormatStorage& f, int sampleRate,
                       int blockSize, int numChannels)
{
    AooFormatPcm_init((AooFormatPcm *)&f, numChannels,
                      sampleRate, blockSize, kAooPcmFloat32);
}

static int32_t getFormatParam(sc_msg_iter *args, const char *name, int32_t def)
{
    if (args->remain() > 0){
        if (args->nextTag() == 's'){
            auto s = args->gets();
            if (strcmp(s, "auto")){
                LOG_ERROR("bad " << name << " argument " << s
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
    std::string_view codec = args->gets("");

    if (codec == kAooCodecNull) {
        auto numChannels = getFormatParam(args, "channels", defNumChannels);
        auto blockSize = getFormatParam(args, "blocksize", unit.bufferSize());
        auto sampleRate = getFormatParam(args, "samplerate", unit.sampleRate());

        AooFormatNull_init((AooFormatNull *)&f, numChannels, sampleRate, blockSize);
    } else if (codec == kAooCodecPcm) {
        auto numChannels = getFormatParam(args, "channels", defNumChannels);
        auto blockSize = getFormatParam(args, "blocksize", unit.bufferSize());
        auto sampleRate = getFormatParam(args, "samplerate", unit.sampleRate());

        AooPcmBitDepth bitdepth = kAooPcmFloat32;
        if (args->remain() > 0) {
            std::string_view b = args->gets("");
            if (b == "int8") {
                bitdepth = kAooPcmInt8;
            } else if (b == "int16") {
                bitdepth = kAooPcmInt16;
            } else if (b == "int24") {
                bitdepth = kAooPcmInt24;
            } else if (b == "float32") {
                bitdepth = kAooPcmFloat32;
            } else if (b == "float64") {
                bitdepth = kAooPcmFloat64;
            } else if (b != "auto") {
                LOG_ERROR("bad bit depth argument, using float32");
            }
        }

        AooFormatPcm_init((AooFormatPcm *)&f, numChannels, sampleRate, blockSize, bitdepth);
    }
#if AOO_USE_OPUS
    else if (codec == kAooCodecOpus) {
        auto numChannels = getFormatParam(args, "channels", defNumChannels);
        auto blockSize = getFormatParam(args, "blocksize", 480); // 10ms
        auto sampleRate = getFormatParam(args, "samplerate", 48000);

        // application type ("auto", "audio", "voip", "lowdelay")
        opus_int32 applicationType;
        if (args->remain() > 0){
            std::string_view type = args->gets("");
            if (type == "auto" || type == "audio") {
                applicationType = OPUS_APPLICATION_AUDIO;
            } else if (type == "voip") {
                applicationType = OPUS_APPLICATION_VOIP;
            } else if (type == "lowdelay") {
                applicationType = OPUS_APPLICATION_RESTRICTED_LOWDELAY;
            } else {
                LOG_ERROR("unsupported application type '" << type << "'");
                return false;
            }
        } else {
            applicationType = OPUS_APPLICATION_AUDIO;
        }

        auto &fmt = (AooFormatOpus &)f;
        AooFormatOpus_init(&fmt, numChannels, sampleRate, blockSize, applicationType);
    }
#endif
    else {
        LOG_ERROR("unknown codec '" << codec << "'");
        return false;
    }
    return true;
}

bool serializeFormat(osc::OutboundPacketStream& msg, const AooFormat& f)
{
    msg << f.codecName << (int32_t)f.numChannels
        << (int32_t)f.blockSize << (int32_t)f.sampleRate;

    std::string_view codec = f.codecName;
    if (codec == kAooCodecNull) {
        return true;
    } else if (codec == kAooCodecPcm) {
        // pcm <channels> <blocksize> <samplerate> <bitdepth>
        auto& fmt = (AooFormatPcm &)f;
        switch (fmt.bitDepth){
        case kAooPcmInt8:
            msg << "int8";
            break;
        case kAooPcmInt16:
            msg << "int16";
            break;
        case kAooPcmInt24:
            msg << "int24";
            break;
        case kAooPcmFloat32:
            msg << "float32";
            break;
        case kAooPcmFloat64:
            msg << "float64";
            break;
        default:
            LOG_ERROR("serializeFormat: bad bit depth argument " << fmt.bitDepth);
            return false;
        }
        return true;
    }
#if AOO_USE_OPUS
    else if (codec == kAooCodecOpus) {
        // opus <channels> <blocksize> <samplerate> <application>
        auto& fmt = (AooFormatOpus &)f;

        // application type
        switch (fmt.applicationType){
        case OPUS_APPLICATION_VOIP:
            msg << "voip";
            break;
        case OPUS_APPLICATION_RESTRICTED_LOWDELAY:
            msg << "lowdelay";
            break;
        case OPUS_APPLICATION_AUDIO:
            msg << "audio";
            break;
        default:
            LOG_ERROR("serializeFormat: bad application type arguments "
                      << fmt.applicationType);
            return false;
        }
        return true;
    }
#endif
    else {
        LOG_ERROR("unknown codec " << f.codecName);
        return false;
    }
}

bool serializeData(osc::OutboundPacketStream& msg, const AooData* data) {
    if (data) {
        msg << data->type << osc::Blob(data->data, data->size);
    } else {
        msg << osc::Nil << osc::Nil;
    }
    return true;
}

std::optional<AooData> parseData(sc_msg_iter *args) {
    if (auto type = args->geti(-1); type >= 0) {
        if (args->nextTag() == 'b') {
            AooData data;
            data.type = type;
            data.size = args->getbsize();
            data.data = (const AooByte *)(args->rdpos + 4);
            args->skipb();
            return data;
        }
    }
    args->geti(); // skip second argument
    return std::nullopt;
}

/*///////////////// Commands ///////////////////*/

struct AooCmd {
    int port;
    aoo::ip_address addr;
};

template<typename T>
void doCommand(World* world, void *replyAddr, T* cmd, AsyncStageFn fn) {
    DoAsynchronousCommand(world, replyAddr, 0, cmd,
                          fn, 0, 0, CmdData::free<T>, 0, 0);
}

void aoo_register(World* world, void* user,
                  sc_msg_iter* args, void* replyAddr) {
    auto port = args->geti();
    auto clientHost = args->gets();
    auto clientPort = args->geti();

    aoo::ip_address addr(clientHost, clientPort, gClientSocketType);

    auto cmdData = CmdData::create<AooCmd>(world);
    if (cmdData) {
        cmdData->port = port;
        cmdData->addr = addr;

        auto fn = [](World* world, void* x) {
            auto data = (AooCmd *)x;

            char buf[1024];
            osc::OutboundPacketStream msg(buf, sizeof(buf));
            msg << osc::BeginMessage("/aoo/register") << data->port;

            auto success = registerClient(data->port, data->addr);

            msg << (int32_t)success << gClientPort << osc::EndMessage;

            sendReply(data->addr, msg);

            return false; // done
        };

        doCommand(world, replyAddr, cmdData, fn);
    }
}

void aoo_unregister(World* world, void* user,
                    sc_msg_iter* args, void* replyAddr) {
    auto port = args->geti();
    auto clientHost = args->gets();
    auto clientPort = args->geti();

    aoo::ip_address addr(clientHost, clientPort, gClientSocketType);

    auto cmdData = CmdData::create<AooCmd>(world);
    if (cmdData) {
        cmdData->port = port;
        cmdData->addr = addr;

        auto fn = [](World* world, void* x) {
            auto data = (AooCmd *)x;

            char buf[1024];
            osc::OutboundPacketStream msg(buf, sizeof(buf));
            msg << osc::BeginMessage("/aoo/unregister") << data->port;

            auto success = unregisterClient(data->port, data->addr);

            msg << (int32_t)success << osc::EndMessage;

            sendReply(data->addr, msg);

            return false; // done
        };

        doCommand(world, replyAddr, cmdData, fn);
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

    AooSettings settings;
    settings.logFunc = SCLog;

    aoo_initialize(&settings);

    Print("AOO (audio over OSC) %s\n", aoo_getVersionString());
    Print("  (c) 2020 Christof Ressi, Winfried Ritsch, et al.\n");

    if (auto [ok, msg] = aoo::check_ntp_server(); ok){
        Print("%s\n", msg.c_str());
    } else {
        Print("WARNING: %s\n", msg.c_str());
    }
    Print("\n");

    AooPluginCmd(aoo_register);
    AooPluginCmd(aoo_unregister);

    AooSendLoad(ft);
    AooReceiveLoad(ft);
    AooClientLoad(ft);
    AooServerLoad(ft);

    try {
        aoo::udp_socket sock(aoo::port_tag{}, 0);
        gClientPort = sock.port();
        gClientSocketType = sock.family();
        gClientSocket = std::move(sock);
        LOG_DEBUG("aoo: create reply socket on port " << gClientPort);
    } catch (const aoo::socket_error& e) {
        LOG_ERROR("aoo: could not create reply socket: " << e.what());
    }
}

// NOTE: at the time of writing (SC 3.13), the 'unload' function is not
// documented in the official plugin API (yet), but it is already called
// by scsynth and Supernova!
C_LINKAGE SC_API_EXPORT void unload() {    
    gClientSocket.close();

    aoo_terminate();
}
