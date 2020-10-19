#include "Aoo.hpp"

// for hardware buffer sizes up to 1024 @ 44.1 kHz
#define DEFBUFSIZE 25

using OpenCmd = _OpenCmd<aoo::isource>;

/*////////////////// AooSend ////////////////*/

class AooSendUnit;

class AooSend : public AooDelegate {
public:
    using AooDelegate::AooDelegate;

    void init(int32_t port, aoo_id id);

    void onDetach() override;

    void doSend() override {
        source_->send();
    }

    void doHandleMessage(const char *data, int32_t size,
                         void *endpoint, aoo_replyfn fn) override
    {
        source_->handle_message(data, size, endpoint, fn);
    }

    void handleEvent(const aoo_event *event);

    aoo::isource * source() { return source_.get(); }

    void addSinkEvent(aoo::endpoint *ep, aoo_id id, int channelOnset);
    bool addSink(aoo::endpoint *ep, aoo_id id, int channelOnset);

    void removeSinkEvent(aoo::endpoint *ep, aoo_id id);
    bool removeSink(aoo::endpoint *ep, aoo_id id);
    void removeAll();

    void setAccept(bool b){
        accept_ = b;
    }
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

