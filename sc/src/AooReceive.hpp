#include "Aoo.hpp"

#define DEFBUFSIZE 50

using OpenCmd = _OpenCmd<aoo::isink>;

/*////////////////// AooReceive ////////////////*/

class AooReceive final : public AooDelegate {
public:
    using AooDelegate::AooDelegate;

    void init(int32_t port, aoo_id id, int32_t bufsize);

    void onDetach() override;

    void handleEvent(const aoo_event *event);

    aoo::isink* sink() { return sink_.get(); }
private:
    aoo::isink::pointer sink_;
};

/*////////////////// AooReceiveUnit ////////////////*/

class AooReceiveUnit final : public AooUnit {
public:
    AooReceiveUnit();

    void next(int numSamples);

    AooReceive& delegate() {
        return static_cast<AooReceive&>(*delegate_);
    }
};
