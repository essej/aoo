#include "Aoo.hpp"

// for hardware buffer sizes up to 1024 @ 44.1 kHz
#define DEFBUFSIZE 25

using OpenCmd = _OpenCmd<aoo::isource>;

/*////////////////// AooSend ////////////////*/

class AooSendUnit;

class AooSend final : public AooDelegate {
public:
    using AooDelegate::AooDelegate;

    void init(int32_t port, aoo_id id);

    void onDetach() override;

    void doSend() override {
        source_->send();
    }

    void doHandleMessage(const char *data, int32_t size,
                         const aoo::ip_address& addr) override
    {
        source_->handle_message(data, size, addr.address(), addr.length());
    }

    void handleEvent(const aoo_event *event);

    aoo::isource * source() { return source_.get(); }

    void addSinkEvent(const aoo::ip_address& addr, aoo_id id, int channelOnset);
    bool addSink(const aoo::ip_address& addr, aoo_id id, int channelOnset);

    void removeSinkEvent(const aoo::ip_address& addr, aoo_id id);
    bool removeSink(const aoo::ip_address& addr, aoo_id id);
    void removeAll();

    void setAccept(bool b){
        accept_ = b;
    }
private:
    aoo::isource::pointer source_;
    bool accept_ = true;
};

/*////////////////// AooSendUnit ////////////////*/

class AooSendUnit final : public AooUnit {
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

