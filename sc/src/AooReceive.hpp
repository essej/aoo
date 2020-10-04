#include "Aoo.hpp"

#define DEFBUFSIZE 50

using OpenCmd = _OpenCmd<aoo::isink>;

/*////////////////// AooReceive ////////////////*/

class AooReceive : public AooDelegate {
public:
    using AooDelegate::AooDelegate;

    void init(int32_t port, int32_t id, int32_t bufsize);

    void onDetach() override;

    void doSend() override {
        sink_->send();
    }

    void doHandleMessage(const char *data, int32_t size,
                         void *endpoint, aoo_replyfn fn) override
    {
        sink_->handle_message(data, size, endpoint, fn);
    }

    void doUpdate() override {
        sink_->decode();
    }

    void handleEvent(const aoo_event *event);

    aoo::isink* sink() { return sink_.get(); }
private:
    aoo::isink::pointer sink_;
};

/*////////////////// AooReceiveUnit ////////////////*/

class AooReceiveUnit : public AooUnit {
public:
    AooReceiveUnit();

    void next(int numSamples);

    AooReceive& delegate() {
        return static_cast<AooReceive&>(*delegate_);
    }
};
