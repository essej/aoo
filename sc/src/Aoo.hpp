#pragma once

#include "SC_PlugIn.hpp"

#include "rt_shared_ptr.hpp"

#include "aoo/aoo.hpp"

#include "common/net_utils.hpp"
#include "common/sync.hpp"
#include "common/utils.hpp"

#include "oscpack/osc/OscOutboundPacketStream.h"

#include <string>
#include <atomic>

/*//////////////////////// Reply //////////////////////////*/

void sendMsgRT(World *world, const osc::OutboundPacketStream& msg);

void sendMsgNRT(World* world, const osc::OutboundPacketStream& msg);

/*//////////////////////// AooNode ////////////////////////*/

namespace aoo {
class endpoint;
class ip_address;
}

class INodeClient {
public:
    virtual ~INodeClient() {}
    virtual void send() = 0;
    virtual void handleMessage(const char *data, int32_t size,
                               void *endpoint, aoo_replyfn fn) = 0;
    virtual void update() {}
};

class INode {
public:
    using ptr = std::shared_ptr<INode>;

    static INode::ptr get(World *world, INodeClient& client,
                          int32_t type, int port, int32_t id);

    virtual ~INode() {}

    virtual void release(INodeClient& client) = 0;

    virtual int socket() const = 0;

    virtual int port() const = 0;

    virtual int sendto(const char *buf, int32_t size,
                       const aoo::ip_address& addr) = 0;

    virtual aoo::endpoint *get_endpoint(const aoo::ip_address& addr) = 0;

    virtual aoo::endpoint *find_peer(const std::string& group,
                                     const std::string& user) = 0;

    virtual void add_peer(const std::string& group, const std::string& user,
                          const aoo::ip_address& addr) = 0;

    virtual void remove_peer(const std::string& group, const std::string& user) = 0;

    virtual void remove_all_peers() = 0;

    virtual void remove_group(const std::string& group) = 0;

    virtual void notify() = 0;
};

/*/////////////////// Commands //////////////////////*/

class AooDelegate;

#define AooPluginCmd(x) DefinePlugInCmd("/" #x, x, 0)

struct CmdData {
    template<typename T>
    static T* create(World *world, size_t extra = 0){
        auto data = doCreate(world, sizeof(T) + extra);
        if (data){
            new (data) T();
        }
        return (T *)data;
    }

    template<typename T>
    static void free(World *world, void *cmdData){
        if (cmdData) {
            auto data = (T*)cmdData;
            // destruct members
            // (e.g. release rt::shared_pointer in RT thread)
            data->~T();
            doFree(world, data);
        }
    }

    bool alive() const;

    // data
    rt::shared_ptr<AooDelegate> owner;
private:
    static void * doCreate(World *world, int size);
    static void doFree(World *world, void *cmdData);
};

template<typename T>
struct _OpenCmd : CmdData {
    int32_t port;
    int32_t id;
    INode::ptr node;
    typename T::pointer obj;
    int32_t sampleRate;
    int32_t blockSize;
    int32_t numChannels;
    int32_t bufferSize;
};

struct OptionCmd : CmdData {
    static OptionCmd *create(World *world, const char *host,
                             int port, int32_t id);

    static OptionCmd* create(World* world, const aoo::endpoint* ep,
                             int32_t id)
    {
        return OptionCmd::create(world,
            ep->address().name(), ep->address().port(), id);
    }

    union {
        float f;
        int i;
        aoo::endpoint *ep;
    };
    int32_t id;
    int32_t port;
    char host[1];
};

struct UnitCmd : CmdData {
    static UnitCmd *create(World *world, sc_msg_iter *args);
    size_t size;
    char data[1];
};

inline void skipUnitCmd(sc_msg_iter *args){
    args->geti(); // node ID
    args->geti(); // synth index
    args->gets(); // command name
}

/*//////////////////////// AooDelegate /////////////////////////*/

class AooUnit;

class AooDelegate :
        public std::enable_shared_from_this<AooDelegate>,
        public INodeClient
{
public:
    AooDelegate(AooUnit& owner);

    ~AooDelegate();

    bool initialized() const {
        return initialized_.load(std::memory_order_acquire);
    }

    bool alive() const {
        return owner_ != nullptr;
    }

    void detach() {
        owner_ = nullptr;
        onDetach();
    }

    World* world() const {
        return world_;
    }

    AooUnit& unit() {
        return *owner_;
    }

    void setNode(INode::ptr node) {
        node_ = std::move(node);
        initialized_.store(true);
    }

    void releaseNode() {
        node_->release(*this);
        node_ = nullptr;
    }

    INode *node() {
        return node_.get();
    }

    // perform sequenced command
    template<typename T>
    void doCmd(T* cmdData, AsyncStageFn stage2, AsyncStageFn stage3 = nullptr,
               AsyncStageFn stage4 = nullptr)
    {
        doCmd(cmdData, stage2, stage3, stage4, CmdData::free<T>);
    }

    // reply messages
    void beginReply(osc::OutboundPacketStream& msg, const char *cmd, int replyID);
    void beginEvent(osc::OutboundPacketStream &msg, const char *event);
    void beginEvent(osc::OutboundPacketStream& msg, const char *event,
                    aoo::endpoint *ep, int32_t id);
    void sendMsgRT(osc::OutboundPacketStream& msg);
    void sendMsgNRT(osc::OutboundPacketStream& msg);
protected:
    virtual void onDetach() = 0;

    void doCmd(CmdData *cmdData, AsyncStageFn stage2, AsyncStageFn stage3,
               AsyncStageFn stage4, AsyncFreeFn cleanup);
private:
    World *world_;
    AooUnit *owner_;
    INode::ptr node_;
    std::atomic<bool> initialized_{false};
};

/*//////////////////////// AooUnit /////////////////////////////*/

class AooUnit : public SCUnit {
public:
    AooUnit(){
        LOG_DEBUG("AooUnit");
        mSpecialIndex = 1;
    }
    ~AooUnit(){
        LOG_DEBUG("~AooUnit");
        delegate_->detach();
    }
    // only returns true after the constructor
    // has been called.
    bool initialized() const {
        return mSpecialIndex != 0;
    }
protected:
    rt::shared_ptr<AooDelegate> delegate_;
};

/*/////////////////////// Helper functions ////////////////*/

bool getSinkArg(INode* node, sc_msg_iter *args,
                aoo::endpoint *& ep, int32_t &id);

bool getSourceArg(INode* node, sc_msg_iter *args,
                  aoo::endpoint *& ep, int32_t &id);

void makeDefaultFormat(aoo_format_storage& f, int sampleRate,
                       int blockSize, int numChannels);

bool parseFormat(const AooUnit& unit, sc_msg_iter *args, aoo_format_storage &f);

void serializeFormat(osc::OutboundPacketStream& msg, const aoo_format& f);
