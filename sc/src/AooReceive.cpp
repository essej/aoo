#include "AooReceive.hpp"

static InterfaceTable* ft;

/*////////////////// AooReceive ////////////////*/

void AooReceive::init(int32_t port, int32_t id, int32 bufsize) {
    auto data = CmdData::create<OpenCmd>(world());
    if (data){
        data->port = port;
        data->id = id;
        data->sampleRate = unit().sampleRate();
        data->blockSize = unit().bufferSize();
        data->numChannels = unit().numOutputs();
        data->bufferSize = bufsize;

        doCmd(data,
            [](World *world, void *data){
                // open in NRT thread
                auto cmd = (OpenCmd *)data;
                auto node = INode::get(world, *cmd->owner, AOO_TYPE_SINK,
                                       cmd->port, cmd->id);
                if (node){
                    auto sink = aoo::isink::create(cmd->id);
                    if (sink){
                        cmd->node = node;

                        sink->setup(cmd->sampleRate, cmd->blockSize,
                                        cmd->numChannels);

                        if (cmd->bufferSize <= 0) {
                            sink->set_buffersize(DEFBUFSIZE);
                        } else {
                            sink->set_buffersize(cmd->bufferSize);
                        }

                        cmd->obj.reset(sink);
                        return true;
                    }
                }
                return false;
            },
            [](World *world, void *data){
                auto cmd = (OpenCmd *)data;
                auto& owner = static_cast<AooReceive&>(*cmd->owner);
                owner.sink_ = std::move(cmd->obj);
                owner.setNode(std::move(cmd->node)); // last!
                LOG_DEBUG("AooReceive initialized");
                return false; // done
            }
        );
    }
}

void AooReceive::onDetach() {
    auto data = CmdData::create<CmdData>(world());
    if (data){
        doCmd(data,
            [](World *world, void *data){
                // release in NRT thread
                auto cmd = (CmdData*)data;
                auto& owner = static_cast<AooReceive&>(*cmd->owner);
                owner.sink_ = nullptr;
                owner.releaseNode();
                return false; // done
            }
        );
    }
}

void AooReceive::handleEvent(const aoo_event *event){
    char buf[256];
    osc::OutboundPacketStream msg(buf, 256);

    switch (event->type){
    case AOO_SOURCE_ADD_EVENT:
    {
        auto e = (aoo_source_event *)event;
        auto ep = (aoo::endpoint *)e->endpoint;

        beginReply(msg, "/add", ep, e->id);
        sendReplyRT(msg);
    }
    case AOO_SOURCE_FORMAT_EVENT:
    {
        auto e = (aoo_source_event *)event;
        auto ep = (aoo::endpoint *)e->endpoint;

        aoo_format_storage f;
        if (sink()->get_source_format(ep, e->id, f) > 0) {
            beginReply(msg, "/format", ep, e->id);
            serializeFormat(msg, f.header);
            sendReplyRT(msg);
        }
        break;
    }
    case AOO_SOURCE_STATE_EVENT:
    {
        auto e = (aoo_source_state_event *)event;
        auto ep = (aoo::endpoint *)e->endpoint;

        beginReply(msg, "/state", ep, e->id);
        msg << e->state;
        sendReplyRT(msg);
        break;
    }
    case AOO_BLOCK_LOST_EVENT:
    {
        auto e = (aoo_block_lost_event *)event;
        auto ep = (aoo::endpoint *)e->endpoint;

        beginReply(msg, "/block/lost", ep, e->id);
        msg << e->count;
        sendReplyRT(msg);
        break;
    }
    case AOO_BLOCK_REORDERED_EVENT:
    {
        auto e = (aoo_block_reordered_event *)event;
        auto ep = (aoo::endpoint *)e->endpoint;

        beginReply(msg, "/block/reordered", ep, e->id);
        msg << e->count;
        sendReplyRT(msg);
        break;
    }
    case AOO_BLOCK_RESENT_EVENT:
    {
        auto e = (aoo_block_resent_event *)event;
        auto ep = (aoo::endpoint *)e->endpoint;

        beginReply(msg, "/block/resent", ep, e->id);
        msg << e->count;
        sendReplyRT(msg);
        break;
    }
    case AOO_BLOCK_GAP_EVENT:
    {
        auto e = (aoo_block_gap_event *)event;
        auto ep = (aoo::endpoint *)e->endpoint;

        beginReply(msg, "/block/gap", ep, e->id);
        msg << e->count;
        sendReplyRT(msg);
        break;
    }
    case AOO_PING_EVENT:
    {
        auto e = (aoo_ping_event *)event;
        auto ep = (aoo::endpoint *)e->endpoint;

        double diff = aoo_osctime_duration(e->tt1, e->tt2);

        beginReply(msg, "/ping", ep, e->id);
        msg << diff;
        sendReplyRT(msg);
        break;
    }
    default:
        break;
    }
}

/*////////////////// AooReceiveUnit ////////////////*/

AooReceiveUnit::AooReceiveUnit() {
    int32_t port = in0(0);
    int32_t id = in0(1);
    int32_t bufsize = in0(2) * 1000.f; // sec -> ms
    auto delegate = rt::make_shared<AooReceive>(mWorld, *this);
    delegate->init(port, id, bufsize);
    delegate_ = std::move(delegate);

    set_calc_function<AooReceiveUnit, &AooReceiveUnit::next>();
}

void AooReceiveUnit::next(int numSamples){
    auto sink = delegate().sink();
    if (sink){
        uint64_t t = aoo_osctime_get();
        if (sink->process(mOutBuf, numSamples, t) <= 0){
            ClearUnitOutputs(this, numSamples);
        }

        if (sink->events_available() > 0){
            sink->handle_events(
                [](void *user, const aoo_event **events, int32_t n){
                    for (int i = 0; i < n; ++i){
                        static_cast<AooReceive *>(user)->handleEvent(events[i]);
                    }
                    return 1;
                }, delegate_.get());
        }
    } else {
        ClearUnitOutputs(this, numSamples);
    }
}

/*//////////////////// Unit commands ////////////////////*/

namespace  {

void aoo_recv_invite(AooReceiveUnit *unit, sc_msg_iter *args){
    auto cmd = UnitCmd::create(unit->mWorld, args);
    unit->delegate().doCmd(cmd,
        [](World *world, void *cmdData){
            auto data = (UnitCmd *)cmdData;
            auto& owner = static_cast<AooReceive&>(*data->owner);

            sc_msg_iter args(data->size, data->data);
            skipUnitCmd(&args);

            aoo::endpoint *ep;
            int32_t id;
            if (getSourceArg(owner.node(), &args, ep, id)){
                owner.sink()->invite_source(ep, id, aoo::endpoint::send);
            }

            return false; // done
        });
}

void aoo_recv_uninvite(AooReceiveUnit *unit, sc_msg_iter *args){
    auto cmd = UnitCmd::create(unit->mWorld, args);
    unit->delegate().doCmd(cmd,
        [](World *world, void *cmdData){
            auto data = (UnitCmd *)cmdData;
            auto& owner = static_cast<AooReceive&>(*data->owner);

            sc_msg_iter args(data->size, data->data);
            skipUnitCmd(&args);

            if (args.remain() > 0){
                aoo::endpoint *ep;
                int32_t id;
                if (getSourceArg(owner.node(), &args, ep, id)){
                    owner.sink()->uninvite_source(ep, id, aoo::endpoint::send);
                }
            } else {
                owner.sink()->uninvite_all();
            }

            return false; // done
        });
}

void aoo_recv_bufsize(AooReceiveUnit *unit, sc_msg_iter *args){
    auto bufSize = args->geti();

    auto cmd = CmdData::create<OptionCmd>(unit->mWorld);
    cmd->i = bufSize;
    unit->delegate().doCmd(cmd,
        [](World *world, void *cmdData){
            auto data = (OptionCmd *)cmdData;
            auto& owner = static_cast<AooReceive&>(*data->owner);
            owner.sink()->set_buffersize(data->i);

            return false; // done
        });
}

void aoo_recv_timefilter(AooReceiveUnit *unit, sc_msg_iter *args){
    auto timefilter = args->getf();
    unit->delegate().sink()->set_timefilter_bandwidth(timefilter);
}

void aoo_recv_packetsize(AooReceiveUnit *unit, sc_msg_iter *args){
    auto packetSize = args->geti();
    unit->delegate().sink()->set_packetsize(packetSize);
}

void aoo_recv_resend(AooReceiveUnit *unit, sc_msg_iter *args){
    auto enable = args->geti();
    unit->delegate().sink()->set_resend_enable(enable);
}

void aoo_recv_resend_limit(AooReceiveUnit *unit, sc_msg_iter *args){
    auto limit = args->geti();
    unit->delegate().sink()->set_resend_maxnumframes(limit);
}

void aoo_recv_resend_interval(AooReceiveUnit *unit, sc_msg_iter *args){
    auto interval = args->geti();
    unit->delegate().sink()->set_resend_interval(interval);
}

void aoo_recv_reset(AooReceiveUnit *unit, sc_msg_iter *args){
    auto cmd = UnitCmd::create(unit->mWorld, args);
    unit->delegate().doCmd(cmd,
        [](World *world, void *cmdData){
            auto data = (UnitCmd *)cmdData;
            auto& owner = static_cast<AooReceive&>(*data->owner);

            sc_msg_iter args(data->size, data->data);
            skipUnitCmd(&args);

            if (args.remain() > 0){
                aoo::endpoint *ep;
                int32_t id;
                if (getSourceArg(owner.node(), &args, ep, id)){
                    owner.sink()->reset_source(ep, id);
                }
            } else {
                owner.sink()->reset();
            }

            return false; // done
        });
}

using AooReceiveUnitCmdFunc = void (*)(AooReceiveUnit*, sc_msg_iter*);

// make sure that unit commands only run after the instance
// has been fully initialized.
template<AooReceiveUnitCmdFunc fn>
static void runUnitCmd(AooReceiveUnit* unit, sc_msg_iter* args) {
    if (unit->initialized() && unit->delegate().initialized()) {
        fn(unit, args);
    } else {
        LOG_WARNING("AooReceive instance not initialized");
    }
}

#define AooUnitCmd(cmd) \
    DefineUnitCmd("AooReceive", "/" #cmd, (UnitCmdFunc)runUnitCmd<aoo_recv_##cmd>)

} // namespace

/*/////////////////// Setup /////////////////////////*/

void AooReceiveLoad(InterfaceTable* inTable) {
    ft = inTable;

    registerUnit<AooReceiveUnit>(ft, "AooReceive");

    AooUnitCmd(invite);
    AooUnitCmd(uninvite);
    AooUnitCmd(bufsize);
    AooUnitCmd(timefilter);
    AooUnitCmd(packetsize);
    AooUnitCmd(resend);
    AooUnitCmd(resend_limit);
    AooUnitCmd(resend_interval);
    AooUnitCmd(reset);
}
