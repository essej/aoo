#include "AooReceive.hpp"

static InterfaceTable* ft;

/*////////////////// AooReceive ////////////////*/

void AooReceive::init(int32_t port, AooId id, AooSeconds latency) {
    auto data = CmdData::create<OpenCmd>(world());
    if (data){
        data->port = port;
        data->id = id;
        data->sampleRate = unit().sampleRate();
        data->blockSize = unit().bufferSize();
        data->numChannels = static_cast<AooReceiveUnit&>(unit()).numChannels();
        data->latency = latency;

        doCmd(data,
            [](World *world, void *data) {
                // open in NRT thread
                auto cmd = (OpenCmd *)data;
                auto node = INode::get(cmd->port);
                if (!node) {
                    return false;
                }
                auto sink = AooSink::create(cmd->id);
                sink->setEventHandler(
                    [](void *user, const AooEvent *event, int32_t){
                        static_cast<AooReceive *>(user)->handleEvent(event);
                    }, cmd->owner.get(), kAooEventModePoll);

                if (node->client()->addSink(sink.get()) == kAooOk) {
                    sink->setup(cmd->numChannels, cmd->sampleRate, cmd->blockSize, 0);

                    if (cmd->latency <= 0) {
                        sink->setLatency(DEFAULT_LATENCY);
                    } else {
                        sink->setLatency(cmd->latency);
                    }

                    cmd->node = std::move(node);
                    cmd->obj = sink.release();

                    return true; // success!
                } else {
                    LOG_ERROR("aoo sink with ID " << cmd->id << " on port "
                              << node->port() << " already exists");
                    return false;
                }
            },
            [](World *world, void *data) {
                auto cmd = (OpenCmd *)data;
                auto& owner = static_cast<AooReceive&>(*cmd->owner);
                owner.sink_.reset(cmd->obj);
                owner.setNode(cmd->node);
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
                auto node = owner.node();
                if (node){
                    // release node
                    node->client()->removeSink(owner.sink());
                    owner.setNode(nullptr);
                }
                // release sink
                owner.sink_ = nullptr;
                return false; // done
            }
        );
    }
}

void AooReceive::handleEvent(const AooEvent *event){
    char buf[256];
    osc::OutboundPacketStream msg(buf, sizeof(buf));

    switch (event->type){
    case kAooEventSourcePing:
    {
        auto& e = event->sourcePing;
        auto delta1 = aoo_ntpTimeDuration(e.t1, e.t2);
        auto delta2 = aoo_ntpTimeDuration(e.t3, e.t4);
        auto total_rtt = aoo_ntpTimeDuration(e.t1, e.t4);
        auto network_rtt = total_rtt - aoo_ntpTimeDuration(e.t2, e.t3);

        beginEvent(msg, "ping", e.endpoint)
            << delta1 << delta2 << network_rtt << total_rtt;
        sendMsgRT(msg);
        break;
    }
    case kAooEventSourceAdd:
    {
        beginEvent(msg, "add", event->sourceAdd.endpoint);
        sendMsgRT(msg);
        break;
    }
    case kAooEventSourceRemove:
    {
        beginEvent(msg, "remove", event->sourceRemove.endpoint);
        sendMsgRT(msg);
        break;
    }
    case kAooEventInviteDecline:
    {
        beginEvent(msg, "decline", event->inviteDecline.endpoint);
        sendMsgRT(msg);
        break;
    }
    case kAooEventInviteTimeout:
    {
        beginEvent(msg, "inviteTimeout", event->inviteTimeout.endpoint);
        sendMsgRT(msg);
        break;
    }
    case kAooEventUninviteTimeout:
    {
        beginEvent(msg, "uninviteTimeout", event->uninviteTimeout.endpoint);
        sendMsgRT(msg);
        break;
    }
    case kAooEventFormatChange:
    {
        auto& e = event->formatChange;
        beginEvent(msg, "format", e.endpoint);
        serializeFormat(msg, *e.format);
        sendMsgRT(msg);
        break;
    }
    case kAooEventStreamStart:
    {
        auto& e = event->streamStart;
        beginEvent(msg, "start", e.endpoint);
        serializeData(msg, e.metadata);
        sendMsgRT(msg);
        break;
    }
    case kAooEventStreamStop:
    {
        beginEvent(msg, "stop", event->streamStop.endpoint);
        sendMsgRT(msg);
        break;
    }
    case kAooEventStreamState:
    {
        beginEvent(msg, "state", event->streamState.endpoint)
            << event->streamState.state;
        sendMsgRT(msg);
        break;
    }
    case kAooEventStreamTime:
    {
        // TODO - is this even useful?
        break;
    }
    case kAooEventStreamLatency:
    {
        auto& e = event->streamLatency;
        beginEvent(msg, "latency", e.endpoint)
            << e.sourceLatency << e.sinkLatency << e.bufferLatency;
        sendMsgRT(msg);
        break;
    }
    case kAooEventBlockDrop:
    {
        beginEvent(msg, "blockDropped", event->blockDrop.endpoint)
            << event->blockDrop.count;
        sendMsgRT(msg);
        break;
    }
    case kAooEventBlockResend:
    {
        beginEvent(msg, "blockResent", event->blockResend.endpoint)
            << event->blockResend.count;
        sendMsgRT(msg);
        break;
    }
    case kAooEventBlockXRun:
    {
        beginEvent(msg, "blockXRun", event->blockXRun.endpoint)
            << event->blockXRun.count;
        sendMsgRT(msg);
        break;
    }
    case kAooEventBufferOverrun:
    {
        beginEvent(msg, "overrun", event->bufferOverrrun.endpoint);
        sendMsgRT(msg);
        break;
    }
    case kAooEventBufferUnderrun:
    {
        beginEvent(msg, "underrun", event->bufferUnderrun.endpoint);
        sendMsgRT(msg);
        break;
    }
    default:
        LOG_DEBUG("AooReceive: got unknown event " << event->type);
        break;
    }
}

/*////////////////// AooReceiveUnit ////////////////*/

AooReceiveUnit::AooReceiveUnit() {
    int port = in0(portIndex);
    AooId id = in0(idIndex);
    AooSeconds latency = in0(latencyIndex);
    numChannels_ = in0(channelIndex);
    assert(numChannels_ >= 0 && numChannels_ <= numOutputs());

    auto delegate = rt::make_shared<AooReceive>(mWorld, *this);
    if (delegate) {
        delegate->init(port, id, latency);
        delegate_ = std::move(delegate);
        // NB: won't do anything because sink hasn't been created yet.
        set_calc_function<AooReceiveUnit, &AooReceiveUnit::next>();
    } else {
        auto unit = this;
        ClearUnitOnMemFailed
    }
}

void AooReceiveUnit::next(int numSamples){
    auto sink = delegate().sink();
    if (sink) {
        uint64_t t = getOSCTime(mWorld);

        auto err = sink->process(mOutBuf, numSamples, t, nullptr, nullptr);
        if (err != kAooErrorIdle) {
            delegate().node()->notify();
        }

        sink->pollEvents();
    } else {
        ClearUnitOutputs(this, numSamples);
    }
}

/*//////////////////// Unit commands ////////////////////*/

namespace  {

void aoo_recv_invite(AooReceiveUnit *unit, sc_msg_iter *args) {
    auto cmd = UnitCmd::create(unit->mWorld, args);
    unit->delegate().doCmd(cmd,
        [](World *world, void *cmdData){
            auto data = (UnitCmd *)cmdData;
            auto& owner = static_cast<AooReceive&>(*data->owner);

            sc_msg_iter args(data->size, data->data);
            skipUnitCmd(&args);

            auto replyID = args.geti();

            char buf[256];
            osc::OutboundPacketStream msg(buf, sizeof(buf));
            owner.beginReply(msg, "/aoo/invite", replyID);

            aoo::ip_address addr;
            AooId id;
            if (owner.node()->getSourceArg(&args, addr, id)){
                AooEndpoint ep { addr.address(),
                            (AooAddrSize)addr.length(), id };

                auto data = parseData(&args);

                if (owner.sink()->inviteSource(ep, data ? &data.value() : nullptr) == kAooOk) {
                    // only send IP address on success
                    msg << (int32_t)1 << addr.name() << addr.port() << id;

                    owner.sendMsgNRT(msg);

                    return false; // success! (done)
                }
            }

            msg << (int32_t)0;
            owner.sendMsgNRT(msg);

            return false; // failure! (done)
        });
}

void aoo_recv_uninvite(AooReceiveUnit *unit, sc_msg_iter *args) {
    auto cmd = UnitCmd::create(unit->mWorld, args);
    unit->delegate().doCmd(cmd,
        [](World *world, void *cmdData){
            auto data = (UnitCmd *)cmdData;
            auto& owner = static_cast<AooReceive&>(*data->owner);

            sc_msg_iter args(data->size, data->data);
            skipUnitCmd(&args);

            auto replyID = args.geti();

            char buf[256];
            osc::OutboundPacketStream msg(buf, sizeof(buf));
            owner.beginReply(msg, "/aoo/uninvite", replyID);

            if (args.remain() > 0){
                aoo::ip_address addr;
                AooId id;
                if (owner.node()->getSourceArg(&args, addr, id)){
                    AooEndpoint ep { addr.address(),
                                (AooAddrSize)addr.length(), id };
                    if (owner.sink()->uninviteSource(ep) != kAooOk) {
                        msg << (int32_t)0;
                        owner.sendMsgNRT(msg);
                        return false; // failure! (done)
                    }
                    // only send IP address on success
                    msg << (int32_t)1 << addr.name() << addr.port() << id;
                }
            } else {
                msg << (int32_t)1;
                owner.sink()->uninviteAll();
            }

            owner.sendMsgNRT(msg);

            return false; // success! (done)
        });
}

void aoo_recv_latency(AooReceiveUnit *unit, sc_msg_iter *args){
    auto cmd = CmdData::create<OptionCmd>(unit->mWorld);
    cmd->f = args->getf();
    unit->delegate().doCmd(cmd,
        [](World *world, void *cmdData){
            auto data = (OptionCmd *)cmdData;
            auto& owner = static_cast<AooReceive&>(*data->owner);
            owner.sink()->setLatency(data->f);

            return false; // done
        });
}

void aoo_recv_buffer_size(AooReceiveUnit *unit, sc_msg_iter *args){
    auto cmd = CmdData::create<OptionCmd>(unit->mWorld);
    cmd->f = args->getf();
    unit->delegate().doCmd(cmd,
        [](World *world, void *cmdData){
            auto data = (OptionCmd *)cmdData;
            auto& owner = static_cast<AooReceive&>(*data->owner);
            owner.sink()->setBufferSize(data->f);

            return false; // done
        });
}

void aoo_recv_packet_size(AooReceiveUnit *unit, sc_msg_iter *args){
    unit->delegate().sink()->setPacketSize(args->geti());
}

void aoo_recv_ping(AooReceiveUnit *unit, sc_msg_iter* args){
    unit->delegate().sink()->setPingInterval(args->getf());
}

void aoo_recv_resend(AooReceiveUnit *unit, sc_msg_iter *args){
    unit->delegate().sink()->setResendData(args->geti());
}

void aoo_recv_resend_limit(AooReceiveUnit *unit, sc_msg_iter *args){
    unit->delegate().sink()->setResendLimit(args->geti());
}

void aoo_recv_resend_interval(AooReceiveUnit *unit, sc_msg_iter *args){
    unit->delegate().sink()->setResendInterval(args->getf());
}

void aoo_recv_reset(AooReceiveUnit *unit, sc_msg_iter *args){
    auto cmd = UnitCmd::create(unit->mWorld, args);
    unit->delegate().doCmd(cmd,
        [](World *world, void *cmdData){
            auto data = (UnitCmd *)cmdData;
            auto& owner = static_cast<AooReceive&>(*data->owner);

            sc_msg_iter args(data->size, data->data);
            skipUnitCmd(&args);

            if (args.remain() > 0) {
                aoo::ip_address addr;
                AooId id;
                if (owner.node()->getSourceArg(&args, addr, id)){
                    AooEndpoint ep { addr.address(),
                                (AooAddrSize)addr.length(), id };
                    owner.sink()->resetSource(ep);
                }
            } else {
                owner.sink()->reset();
            }

            return false; // done
        });
}

void aoo_recv_dynamic_resampling(AooReceiveUnit *unit, sc_msg_iter *args){
    unit->delegate().sink()->setDynamicResampling(args->geti());
}

void aoo_recv_dll_bw(AooReceiveUnit *unit, sc_msg_iter *args){
    unit->delegate().sink()->setDllBandwidth(args->getf());
}

using AooReceiveUnitCmdFunc = void (*)(AooReceiveUnit*, sc_msg_iter*);

// make sure that unit commands only run after the instance
// has been fully initialized.
template<AooReceiveUnitCmdFunc fn>
static void runUnitCmd(AooReceiveUnit* unit, sc_msg_iter* args) {
    if (unit->initialized() && unit->delegate().initialized()) {
        fn(unit, args);
    } else {
        LOG_WARNING("AooReceive: instance not initialized");
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
    AooUnitCmd(latency);
    AooUnitCmd(ping);
    AooUnitCmd(packet_size);
    AooUnitCmd(buffer_size);
    AooUnitCmd(resend);
    AooUnitCmd(resend_limit);
    AooUnitCmd(resend_interval);
    AooUnitCmd(reset);
    AooUnitCmd(dynamic_resampling);
    AooUnitCmd(dll_bw);
}
