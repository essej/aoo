#include "Aoo.hpp"

#define DEFBUFSIZE 50

using OpenCmd = _OpenCmd<aoo::isink>;

/*////////////////// AooReceive ////////////////*/

class AooReceive : public AooDelegate {
public:
    using AooDelegate::AooDelegate;

    void init(int32_t port, int32_t id) override;

    void onDetach() override;

    void send() override {
        if (initialized()){
            sink_->send();
        }
    }

    void handleMessage(const char *data, int32_t size,
                       void *endpoint, aoo_replyfn fn) override
    {
        if (initialized()){
            sink_->handle_message(data, size, endpoint, fn);
        }
    }

    void update() override {
        if (initialized()){
            sink_->decode();
        }
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
