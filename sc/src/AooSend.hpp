#include "Aoo.hpp"

// for hardware buffer sizes up to 1024 @ 44.1 kHz
#define DEFBUFSIZE 25

using OpenCmd = _OpenCmd<aoo::isource>;

/*////////////////// AooSend ////////////////*/

class AooSendUnit;

class AooSend : public AooDelegate {
public:
    using AooDelegate::AooDelegate;

    void init(int32_t port, int32_t id) override;

    void onDetach() override;

    void send() override {
        if (initialized()){
            source_->send();
        }
    }

    void handleMessage(const char *data, int32_t size,
                       void *endpoint, aoo_replyfn fn) override
    {
        if (initialized()){
            source_->handle_message(data, size, endpoint, fn);
        }
    }

    void handleEvent(const aoo_event *event);

    aoo::isource * source() { return source_.get(); }

    void addSink(aoo::endpoint *ep, int32_t id, int channelOnset);
    void doAddSink(aoo::endpoint *ep, int32_t id, int channelOnset);

    void removeSink(aoo::endpoint *ep, int32_t id);
    void doRemoveSink(aoo::endpoint *ep, int32_t id);

    void removeAll();
    void doRemoveAll();

    void setAccept(bool b){
        accept_ = b;
    }

    void setFormat(aoo_format& f);
private:
    aoo::isource::pointer source_;
    bool accept_ = true;
};

/*////////////////// AooSendUnit ////////////////*/

class AooSendUnit : public AooUnit {
public:
    AooSendUnit();

    void next(int numSamples);

    AooSend& delegate() {
        return static_cast<AooSend&>(*delegate_);
    }

    int numChannels() const {
        return numInputs() - channelOnset_;
    }
private:
    static const int channelOnset_ = 3;

    bool playing_ = false;
};

