#include "Aoo.hpp"

#include "aoo_source.hpp"

// for hardware buffer sizes up to 2048 @ 44.1 kHz
#define DEFBUFSIZE 0.05

using OpenCmd = OpenCmd_<AooSource>;

/*////////////////// AooSend ////////////////*/

class AooSendUnit;

class AooSend final : public AooDelegate {
public:
    using AooDelegate::AooDelegate;

    void init(int32_t port, AooId id);

    void onDetach() override;

    void handleEvent(const AooEvent *event);

    AooSource * source() { return source_.get(); }

    bool addSink(const aoo::ip_address& addr, AooId id,
                 int32_t chan, bool active);

    bool removeSink(const aoo::ip_address& addr, AooId id);

    void removeAllSinks();

    void startStream(int32_t offset, const AooData* metadata) {
        source_->startStream(offset, metadata);
        running_ = true;
    }

    void stopStream(int32_t offset) {
        source_->stopStream(offset);
        running_ = false;
    }

    void setAutoInvite(bool b){
        autoInvite_ = b;
    }
private:
    AooSource::Ptr source_;
    bool running_ = false;
    bool autoInvite_ = true;
};

/*////////////////// AooSendUnit ////////////////*/

class AooSendUnit final : public AooUnit {
public:
    AooSendUnit();

    void next(int numSamples);

    AooSend& delegate() {
        return static_cast<AooSend&>(*delegate_);
    }

    int numChannels() const { return numChannels_; }
private:
    static const int portIndex = 0;
    static const int idIndex = 1;
    static const int gateIndex = 2;
    static const int channelIndex = 3;
    static const int bufferIndex = 4;

    int numChannels_ = 0;
    float lastGate_ = 0;
};

